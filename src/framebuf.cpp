/*
 * Framebuffer implementation, rewritten from MicroPython modframebuf.c
 *
 * This version (c) 2024-2026 Erik Tkal
 *
 * Original copyright:
 *
 * This file is part of the MicroPython project, http://micropython.org/
 * The MIT License (MIT)
 * Copyright (c) 2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "framebuf.h"

#include <algorithm>
#include <iostream>

#include "font.h"
#include "font_petme128_8x8.h"

using std::max;
using std::min;
using std::size_t;

Framebuf::Framebuf()
    : m_pBuf(nullptr),
      m_nPixelSize(0),
      m_nWidth(0),
      m_nHeight(0),
      m_nStride(0),
      m_eFormat(DISPLAY_COLOUR_FORMAT),
      m_bRevBytes(false),
      m_pFont(nullptr)
{
}

Framebuf::~Framebuf()
{
    if (nullptr == m_pBuf)
    {
        return;
    }
    switch (m_eFormat)
    {
    case MVLSB:
    case MHLSB:
    case MHMSB:
        delete[] (uint8_t*)m_pBuf;
        break;
    case RGB565:
        delete[] (uint16_t*)m_pBuf;
        break;
    case RGB666:
        delete[] (pixel666*)m_pBuf;
        break;
    default:
        break;
    }
}

void Framebuf::Initialize(uint16_t nWidth, uint16_t nHeight, ePixelFormat eFormat, bool bRevBytes, uint16_t nStride)
{
    m_nWidth = nWidth;
    m_nHeight = nHeight;
    m_eFormat = eFormat;
    m_bRevBytes = bRevBytes;
    if (0 == nStride)
    {
        m_nStride = m_nWidth;
    }
    switch (m_eFormat)
    {
    case MVLSB:
    case MHLSB:
    case MHMSB:
        if (nullptr != m_pBuf)
        {
            delete[] (uint8_t*)m_pBuf;
        }
        m_pBuf = new uint8_t[m_nWidth * m_nHeight / 8];
        m_nStride = (m_nStride + 7) & ~7;
        m_nPixelSize = 1;
        break;
    case RGB565:
        if (nullptr != m_pBuf)
        {
            delete[] (uint16_t*)m_pBuf;
        }
        m_pBuf = new uint16_t[m_nWidth * m_nHeight];
        m_nPixelSize = 2;
        break;
    case RGB666:
        if (nullptr != m_pBuf)
        {
            delete[] (pixel666*)m_pBuf;
        }
        m_pBuf = new pixel666[m_nWidth * m_nHeight];
        m_nPixelSize = 3;
        break;
    default:
        m_pBuf = nullptr;
        break;
    }
}

void Framebuf::setpixel(int x, int y, uint16_t color)
{
    if (!check(x, y) || nullptr == m_pBuf)
    {
        return;
    }

    switch (m_eFormat)
    {
    case MVLSB:
    {
        size_t index = (y >> 3) * m_nStride + x;
        uint8_t offset = y & 0x07;
        ((uint8_t*)m_pBuf)[index] = (((uint8_t*)m_pBuf)[index] & ~(0x01 << offset)) | ((color != 0) << offset);
    }
    break;
    case RGB565:
    {
        ((uint16_t*)m_pBuf)[x + y * m_nStride] = fixcolor(color);
    }
    break;
    case RGB666:
    {
        ((pixel666*)m_pBuf)[x + y * m_nStride] = color;
    }
    break;
    default:
        return;
        break;
    }
}

uint16_t Framebuf::getpixel(int x, int y)
{
    if (!check(x, y) || nullptr == m_pBuf)
    {
        return 0;
    }
    switch (m_eFormat)
    {
    case MVLSB:
        return (((uint8_t*)m_pBuf)[(y >> 3) * m_nStride + x] >> (y & 0x07)) & 0x01;
        break;
    case RGB565:
        return ((uint16_t*)m_pBuf)[x + y * m_nStride];
        break;
    case RGB666:
        return ((pixel666*)m_pBuf)[x + y * m_nStride];
        break;
    default:
        return 0;
        break;
    }
}

void Framebuf::fillrect(int x, int y, int w, int h, uint16_t color)
{
    if (!check(x, y, w, h) || nullptr == m_pBuf)
    {
        return;
    }
    switch (m_eFormat)
    {
    case MVLSB:
        while (h--)
        {
            uint8_t* b = &((uint8_t*)m_pBuf)[(y >> 3) * m_nStride + x];
            uint8_t offset = y & 0x07;
            for (unsigned int ww = w; ww; --ww)
            {
                *b = (*b & ~(0x01 << offset)) | ((color != 0) << offset);
                ++b;
            }
            ++y;
        }
        break;
    case RGB565:
    {
        uint16_t* b = &((uint16_t*)m_pBuf)[x + y * m_nStride];
        while (h--)
        {
            for (unsigned int ww = w; ww; --ww)
            {
                *b++ = fixcolor(color);
            }
            b += m_nStride - w;
        }
    }
    break;
    case RGB666:
    {
        pixel666* b = &((pixel666*)m_pBuf)[x + y * m_nStride];
        while (h--)
        {
            for (unsigned int ww = w; ww; --ww)
            {
                *b++ = color;
            }
            b += m_nStride - w;
        }
    }
    break;
    default:
        break;
    }
}

void Framebuf::fill(uint16_t color)
{
    fillrect(0, 0, m_nWidth, m_nHeight, color);
}

void Framebuf::hline(int x, int y, int w, uint16_t color)
{
    fillrect(x, y, w, 1, color);
}

void Framebuf::vline(int x, int y, int h, uint16_t color)
{
    fillrect(x, y, 1, h, color);
}

void Framebuf::rect(int x, int y, int w, int h, uint16_t color, bool bFill)
{
    if (bFill)
    {
        fillrect(x, y, w, h, color);
    }
    else
    {
        fillrect(x, y, w, 1, color);
        fillrect(x, y + h - 1, w, 1, color);
        fillrect(x, y, 1, h, color);
        fillrect(x + w - 1, y, 1, h, color);
    }
}

void Framebuf::line(int x1, int y1, int x2, int y2, uint16_t color)
{
    int dx = x2 - x1;
    int sx;
    if (dx > 0)
    {
        sx = 1;
    }
    else
    {
        dx = -dx;
        sx = -1;
    }

    int dy = y2 - y1;
    int sy;
    if (dy > 0)
    {
        sy = 1;
    }
    else
    {
        dy = -dy;
        sy = -1;
    }

    bool steep;
    if (dy > dx)
    {
        int temp;
        temp = x1;
        x1 = y1;
        y1 = temp;
        temp = dx;
        dx = dy;
        dy = temp;
        temp = sx;
        sx = sy;
        sy = temp;
        steep = true;
    }
    else
    {
        steep = false;
    }

    int e = 2 * dy - dx;
    for (int i = 0; i < dx; ++i)
    {
        if (steep)
        {
            if (0 <= y1 && y1 < m_nWidth && 0 <= x1 && x1 < m_nHeight)
            {
                setpixel(y1, x1, color);
            }
        }
        else
        {
            if (0 <= x1 && x1 < m_nWidth && 0 <= y1 && y1 < m_nHeight)
            {
                setpixel(x1, y1, color);
            }
        }
        while (e >= 0)
        {
            y1 += sy;
            e -= 2 * dx;
        }
        x1 += sx;
        e += 2 * dy;
    }

    setpixel(x2, y2, color);
}

void Framebuf::ellipse(int cx, int cy, int xradius, int yradius, uint16_t color, bool bFill, uint8_t mask)
{
    if (bFill)
    {
        mask |= ELLIPSE_MASK_FILL;
    }
    int two_asquare = 2 * xradius * xradius;
    int two_bsquare = 2 * yradius * yradius;
    int x = xradius;
    int y = 0;
    int xchange = yradius * yradius * (1 - 2 * xradius);
    int ychange = xradius * xradius;
    int ellipse_error = 0;
    int stoppingx = two_bsquare * xradius;
    int stoppingy = 0;
    while (stoppingx >= stoppingy)
    { // 1st set of points,  y' > -1
        ellipse_points(cx, cy, x, y, color, mask);
        y += 1;
        stoppingy += two_asquare;
        ellipse_error += ychange;
        ychange += two_asquare;
        if ((2 * ellipse_error + xchange) > 0)
        {
            x -= 1;
            stoppingx -= two_bsquare;
            ellipse_error += xchange;
            xchange += two_bsquare;
        }
    }
    // 1st point set is done start the 2nd set of points
    x = 0;
    y = yradius;
    xchange = yradius * yradius;
    ychange = xradius * xradius * (1 - 2 * yradius);
    ellipse_error = 0;
    stoppingx = 0;
    stoppingy = two_asquare * yradius;
    while (stoppingx <= stoppingy)
    { // 2nd set of points, y' < -1
        ellipse_points(cx, cy, x, y, color, mask);
        x += 1;
        stoppingx += two_bsquare;
        ellipse_error += xchange;
        xchange += two_bsquare;
        if ((2 * ellipse_error + ychange) > 0)
        {
            y -= 1;
            stoppingy -= two_asquare;
            ellipse_error += ychange;
            ychange += two_asquare;
        }
    }
    return;
}

void Framebuf::text(const char* str, int x, int y, uint16_t color)
{
    // If a default font is set, use it
    if (m_pFont)
    {
        return text(str, x, y, color, *m_pFont, 1);
    }

    // loop over chars
    for (; *str; ++str)
    {
        // get char and make sure its in range of font
        int chr = *(uint8_t*)str;
        if (chr < 32 || chr > 127)
        {
            chr = 127;
        }
        // get char data
        const uint8_t* chr_data = &font_petme128_8x8[(chr - 32) * 8];
        // loop over char data
        for (int j = 0; j < 8; j++, x++)
        {
            if (0 <= x && x < m_nWidth)
            {                                                        // clip x
                uint vline_data = chr_data[j];                       // each byte is a column of 8 pixels, LSB at top
                for (int y1 = y; vline_data; vline_data >>= 1, y1++) // scan over vertical column
                {
                    if (vline_data & 1) // only draw if pixel set
                    {
                        if (0 <= y1 && y1 < m_nHeight) // clip y
                        {
                            setpixel(x, y1, color);
                        }
                    }
                }
            }
        }
    }
}

void Framebuf::text(const char* str, int x, int y, uint16_t color, int scale)
{
    if (scale <= 1)
    {
        return text(str, x, y, color);
    }

    // If a default font is set, use it
    if (m_pFont)
    {
        return text(str, x, y, color, *m_pFont, scale);
    }

    // loop over chars
    for (; *str; ++str)
    {
        int chr = *(uint8_t*)str;
        if (chr < 32 || chr > 127)
        {
            chr = 127;
        }
        const uint8_t* chr_data = &font_petme128_8x8[(chr - 32) * 8];

        int xchar = x; // starting x for this character
        for (int j = 0; j < 8; j++, xchar += scale)
        {
            if (0 <= xchar && xchar < m_nWidth)
            {
                uint vline_data = chr_data[j];
                for (int row = 0; vline_data; vline_data >>= 1, ++row)
                {
                    if (vline_data & 1)
                    {
                        int ybase = y + row * scale;
                        for (int sx = 0; sx < scale; ++sx)
                        {
                            for (int sy = 0; sy < scale; ++sy)
                            {
                                int px = xchar + sx;
                                int py = ybase + sy;
                                if (0 <= px && px < m_nWidth && 0 <= py && py < m_nHeight)
                                {
                                    setpixel(px, py, color);
                                }
                            }
                        }
                    }
                }
            }
        }
        x += 8 * scale; // advance to next character
    }
}

void Framebuf::text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale)
{
    if (scale < 1)
    {
        scale = 1;
    }

    const int first = font.firstChar;
    const int count = font.charCount;
    const int gw = font.width;
    const int gh = font.height;
    const int rowBytes = font.rowBytes();
    const size_t glyphSize = rowBytes * gh;

    if (font.colMajor)
    {
        // Column-major format: bytes represent columns (e.g., PetMe 8x8)
        // Each byte is a column, with LSB=top, MSB=bottom
        for (; *str; ++str)
        {
            int chr = (uint8_t)*str;
            if (chr < first || chr >= first + count)
            {
                chr = first + count - 1;
            }
            const uint8_t* glyph = font.data + (chr - first) * glyphSize;

            // For column-major: iterate columns (rx), then rows within that column
            for (int rx = 0; rx < gw; ++rx)
            {
                uint8_t col_byte = glyph[rx];
                for (int ry = 0; ry < gh; ++ry)
                {
                    uint8_t mask = 1 << (ry % 8); // LSB is top
                    if (col_byte & mask)
                    {
                        int xbase = x + rx * scale;
                        int ybase = y + ry * scale;
                        for (int sx = 0; sx < scale; ++sx)
                        {
                            for (int sy = 0; sy < scale; ++sy)
                            {
                                int px = xbase + sx;
                                int py = ybase + sy;
                                if (0 <= px && px < (int)m_nWidth && 0 <= py && py < (int)m_nHeight)
                                {
                                    setpixel(px, py, color);
                                }
                            }
                        }
                    }
                }
            }
            x += gw * scale;
        }
    }
    else
    {
        // Row-major format: bytes represent rows (standard, e.g., Terminus fonts)
        for (; *str; ++str)
        {
            int chr = (uint8_t)*str;
            if (chr < first || chr >= first + count)
            {
                chr = first + count - 1;
            }
            const uint8_t* glyph = font.data + (chr - first) * glyphSize;

            for (int ry = 0; ry < gh; ++ry)
            {
                const uint8_t* rowPtr = glyph + ry * rowBytes;
                for (int rx = 0; rx < gw; ++rx)
                {
                    int byteIndex = rx / 8;
                    uint8_t mask = 0x80 >> (rx % 8);
                    if (rowPtr[byteIndex] & mask)
                    {
                        int xbase = x + rx * scale;
                        int ybase = y + ry * scale;
                        for (int sx = 0; sx < scale; ++sx)
                        {
                            for (int sy = 0; sy < scale; ++sy)
                            {
                                int px = xbase + sx;
                                int py = ybase + sy;
                                if (0 <= px && px < (int)m_nWidth && 0 <= py && py < (int)m_nHeight)
                                {
                                    setpixel(px, py, color);
                                }
                            }
                        }
                    }
                }
            }
            x += gw * scale;
        }
    }
}

// Private methods

bool Framebuf::check(int& x, int& y)
{
    return (0 <= x && x < m_nWidth && 0 <= y && y < m_nHeight);
}

bool Framebuf::check(int& x, int& y, int& w, int& h)
{
    if (w < 1 || h < 1 || x + w <= 0 || y + h <= 0 || x >= m_nWidth || y >= m_nHeight)
    {
        return false;
    }
    // clip to the framebuffer size
    int xend = min((int)m_nWidth, x + w);
    int yend = min((int)m_nHeight, y + h);
    x = max(x, 0);
    y = max(y, 0);
    w = xend - x;
    h = yend - y;
    return true;
}

void Framebuf::ellipse_points(int cx, int cy, int x, int y, uint16_t color, uint8_t mask)
{
    if (mask & ELLIPSE_MASK_FILL)
    {
        if (mask & ELLIPSE_MASK_Q1)
        {
            fillrect(cx, cy - y, x + 1, 1, color);
        }
        if (mask & ELLIPSE_MASK_Q2)
        {
            fillrect(cx - x, cy - y, x + 1, 1, color);
        }
        if (mask & ELLIPSE_MASK_Q3)
        {
            fillrect(cx - x, cy + y, x + 1, 1, color);
        }
        if (mask & ELLIPSE_MASK_Q4)
        {
            fillrect(cx, cy + y, x + 1, 1, color);
        }
    }
    else
    {
        setpixel_masked(cx + x, cy - y, color, mask & ELLIPSE_MASK_Q1);
        setpixel_masked(cx - x, cy - y, color, mask & ELLIPSE_MASK_Q2);
        setpixel_masked(cx - x, cy + y, color, mask & ELLIPSE_MASK_Q3);
        setpixel_masked(cx + x, cy + y, color, mask & ELLIPSE_MASK_Q4);
    }
}

void Framebuf::setpixel_masked(int x, int y, uint16_t color, uint8_t mask)
{
    if (0 != mask && check(x, y))
    {
        setpixel(x, y, color);
    }
}

void Framebuf::scroll(int xstep, int ystep)
{
    int sx, y, xend, yend, dx, dy;
    if (xstep < 0)
    {
        sx = 0;
        xend = m_nWidth + xstep;
        if (xend <= 0)
        {
            return;
        }
        dx = 1;
    }
    else
    {
        sx = m_nWidth - 1;
        xend = xstep - 1;
        if (xend >= sx)
        {
            return;
        }
        dx = -1;
    }
    if (ystep < 0)
    {
        y = 0;
        yend = m_nHeight + ystep;
        if (yend <= 0)
        {
            return;
        }
        dy = 1;
    }
    else
    {
        y = m_nHeight - 1;
        yend = ystep - 1;
        if (yend >= y)
        {
            return;
        }
        dy = -1;
    }
    for (; y != yend; y += dy)
    {
        for (int x = sx; x != xend; x += dx)
        {
            setpixel(x, y, getpixel(x - xstep, y - ystep));
        }
    }
}
