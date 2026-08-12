/*
 * Pico ILI TFT display driver class
 *
 * Currently supports ILI9341 and ILI9488 displays.
 *
 * (c) 2024-2026 Erik Tkal
 *
 * Modified from Darren Horrocks version to fix command/data/select timing, as well
 * as removing the GFXFont support.
 * Modified to inherit from Framebuffer (itself a version modified from analyzing
 * the Damien P. George MicroPython modframebuf.c).
 */

/*
BSD 3-Clause License

Copyright (c) 2022, Darren Horrocks
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "stdlib.h"
#include "stdio.h"
#include <iostream>
#include <cstring>
#include <vector>

#include "ili_tft.h"
#include "hardware/gpio.h"

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#endif
#ifndef __swap_int
#define __swap_int(a, b)   \
    a = (a & b) + (a | b); \
    b = a + (~b) + 1;      \
    a = a + (~b) + 1;
#endif

ILI_TFT::ILI_TFT(spi_inst_t* spi, uint8_t cs, uint8_t dc, uint8_t rst, ROTATION rotation)
    : m_spi(spi),
      m_cs(cs),
      m_dc(dc),
      m_rst(rst),
      m_dispWidth(0),
      m_dispHeight(0),
      m_rotation(rotation),
      m_madctl(DISPLAY_COLOUR_ORDER),
      m_nQuadrants(DISPLAY_QUADRANTS),
      m_eQuadrant(FULL_FRAME),
      m_xoff(0),
      m_yoff(0)
{
    switch (DISPLAY_COLOUR_FORMAT)
    {
    case RGB666:
        m_colmod = 0x66; // 18-bit/pixel
        break;
    case RGB565:
    default:
        m_colmod = 0x55; // 16-bit/pixel
        break;
    }
}

#if defined(DISPLAY_ILI934X)
ILI934X::ILI934X(spi_inst_t* spi, uint8_t cs, uint8_t dc, uint8_t rst, ROTATION rotation)
    : ILI_TFT(spi, cs, dc, rst, rotation)
{
}

void ILI934X::Reset()
{
    gpio_put(m_rst, 1);
    sleep_ms(50);
    gpio_put(m_rst, 0);
    sleep_ms(50);
    gpio_put(m_rst, 1);
    sleep_ms(50);
}

void ILI934X::Initialize()
{
    setRotation(ILI934X_HW_WIDTH, ILI934X_HW_HEIGHT, m_rotation); // Sets width, height and MADCTL value
    createFramebuf();

    // Reset the display
    Reset();

    // Set the registers
    writeCmd(_RDDSDR, (uint8_t*)"\x03\x80\x02", 3);
    writeCmd(_PWCRTLB, (uint8_t*)"\x00\xc1\x30", 3);
    writeCmd(_PWRONCTRL, (uint8_t*)"\x64\x03\x12\x81", 4);
    writeCmd(_DTCTRLA, (uint8_t*)"\x85\x00\x78", 3);
    writeCmd(_PWCTRLA, (uint8_t*)"\x39\x2c\x00\x34\x02", 5);
    writeCmd(_PRCTRL, (uint8_t*)"\x20", 1);
    writeCmd(_DTCTRLB, (uint8_t*)"\x00\x00", 2);
    writeCmd(_PWCTRL1, (uint8_t*)"\x23", 1);
    writeCmd(_PWCTRL2, (uint8_t*)"\x10", 1);
    writeCmd(_VMCTRL1, (uint8_t*)"\x3e\x28", 2);
    writeCmd(_VMCTRL2, (uint8_t*)"\x86", 1);
    writeCmd(_MADCTL, &m_madctl, 1);
    writeCmd(_COLMOD, &m_colmod, 1);
    writeCmd(_FRMCTR1, (uint8_t*)"\x00\x18", 2);
    writeCmd(_DISCTRL, (uint8_t*)"\x08\x82\x27", 3);
    writeCmd(_ENA3G, (uint8_t*)"\x00", 1);
    writeCmd(_GAMSET, (uint8_t*)"\x01", 1);
    writeCmd(_PGAMCTRL, (uint8_t*)"\x0f\x31\x2b\x0c\x0e\x08\x4e\xf1\x37\x07\x10\x03\x0e\x09\x00", 15);
    writeCmd(_NGAMCTRL, (uint8_t*)"\x00\x0e\x14\x03\x11\x07\x31\xc1\x48\x08\x0f\x0c\x31\x36\x0f", 15);

    writeCmd(_SLPOUT);
    writeCmd(_DISPON);
}

// ILI934X sends one byte of data
void ILI934X::sendData(uint8_t data)
{
    cs_select();
    data_select();

    writeByte(data);

    cs_deselect();
}

void ILI934X::sendFramebufferData(uint8_t* data, size_t dataLen)
{
    ILI_TFT::sendData(data, dataLen);
}
#endif // DISPLAY_ILI934X

#if defined(DISPLAY_ILI948X)
ILI948X::ILI948X(spi_inst_t* spi, uint8_t cs, uint8_t dc, uint8_t rst, ROTATION rotation)
    : ILI_TFT(spi, cs, dc, rst, rotation)
{
}

void ILI948X::Reset()
{
    gpio_put(m_rst, 1);
    sleep_ms(50);
    gpio_put(m_rst, 0);
    sleep_ms(50);
    gpio_put(m_rst, 1);
    sleep_ms(50);
}

void ILI948X::Initialize()
{
    setRotation(ILI948X_HW_WIDTH, ILI948X_HW_HEIGHT, m_rotation); // Sets width, height and MADCTL value
    createFramebuf();

    // Reset the display
    Reset();

    // Set the registers
    writeCmd(_DSPINVON);
    writeCmd(_PWCTRL3, (uint8_t*)"\x33", 1);
    writeCmd(_VMCTRL1, (uint8_t*)"\x00\x1e\x80", 3);
    writeCmd(_MADCTL, &m_madctl, 1);
    writeCmd(_COLMOD, &m_colmod, 1);
    writeCmd(_FRMCTR1, (uint8_t*)"\xb0", 1);
    writeCmd(_DISCTRL, (uint8_t*)"\x00\x02", 2);
    writeCmd(_PGAMCTRL, (uint8_t*)"\x00\x13\x18\x04\x0f\x06\x3a\x56\x4d\x03\x0a\x06\x30\x3e\x0f", 15);
    writeCmd(_NGAMCTRL, (uint8_t*)"\x00\x13\x18\x01\x11\x06\x38\x34\x4d\x06\x0d\x0b\x31\x37\x0f", 15);
    writeCmd(_SLPOUT);
    sleep_ms(50);
    writeCmd(_DISPON);
}

// ILI948X command parameters are single-byte values
void ILI948X::sendData(uint8_t data)
{
    cs_select();
    data_select();

    writeByte(data);

    cs_deselect();
}

void ILI948X::sendFramebufferData(uint8_t* data, size_t dataLen)
{
    ILI_TFT::sendData(data, dataLen);
}
#endif // DISPLAY_ILI948X

#if defined(DISPLAY_ST7796)
ST7796::ST7796(spi_inst_t* spi, uint8_t cs, uint8_t dc, uint8_t rst, ROTATION rotation)
    : ILI_TFT(spi, cs, dc, rst, rotation)
{
}

void ST7796::Reset()
{
    gpio_put(m_rst, 1);
    sleep_ms(50);
    gpio_put(m_rst, 0);
    sleep_ms(50);
    gpio_put(m_rst, 1);
    sleep_ms(50);
}

void ST7796::Initialize()
{
    setRotation(ST7796_HW_WIDTH, ST7796_HW_HEIGHT, m_rotation); // Sets width, height and MADCTL value
    createFramebuf();

    // Reset the display
    Reset();

    // Set the registers
    writeCmd(0x01); // Software reset
    sleep_ms(5);
    writeCmd(_SLPOUT); // Sleep exit
    sleep_ms(120);
    writeCmd(_CSCON, (uint8_t*)"\xC3", 1);
    writeCmd(_CSCON, (uint8_t*)"\x96", 1);
    writeCmd(_MADCTL, &m_madctl, 1);
    writeCmd(_COLMOD, &m_colmod, 1);
    writeCmd(_DISCTRL, (uint8_t*)"\x00\x02", 2);
    writeCmd(_DTCTRLA, (uint8_t*)"\x40\x8A\x00\x00\x29\x19\xA5\x33", 8);
    writeCmd(_PWCTRL3);
    writeCmd(_VMCTRL1, (uint8_t*)"\x24", 1);
    writeCmd(_PGAMCTRL, (uint8_t*)"\xF0\x09\x13\x12\x12\x2B\x3C\x44\x4B\x1B\x18\x17\x1D\x21", 14);
    writeCmd(_NGAMCTRL, (uint8_t*)"\xF0\x09\x13\x0C\x0D\x27\x3B\x44\x4D\x0B\x17\x17\x1D\x21", 14);
    writeCmd(_CSCON, (uint8_t*)"\x3C", 1);
    writeCmd(_CSCON, (uint8_t*)"\x69", 1);
    writeCmd(_SLPOUT);
    sleep_ms(50);
    writeCmd(_DISPON);
}

// ILI948X command parameters are single-byte values
void ST7796::sendData(uint8_t data)
{
    cs_select();
    data_select();

    writeByte(data);

    cs_deselect();
}

void ST7796::sendFramebufferData(uint8_t* data, size_t dataLen)
{
    ILI_TFT::sendData(data, dataLen);
}
#endif // DISPLAY_ST7796

void ILI_TFT::Clear(uint16_t colour)
{
    for (auto nQuadrant : GetQuadrants())
    {
        SetQuadrant(nQuadrant);
        Fill(COLOUR_BLACK);
        Show();
    }
}

void ILI_TFT::SetQuadrant(QUADRANT eQuadrant)
{
    m_eQuadrant = eQuadrant;
    switch (m_eQuadrant)
    {
    case FULL_FRAME:
    case LEFT_HALF:
    case UPPER_HALF:
    case UPPER_LEFT:
        m_xoff = 0;
        m_yoff = 0;
        break;
    case RIGHT_HALF:
    case UPPER_RIGHT:
        m_xoff = m_dispWidth / 2;
        m_yoff = 0;
        break;
    case LOWER_HALF:
    case LOWER_LEFT:
        m_xoff = 0;
        m_yoff = m_dispHeight / 2;
        break;
    case LOWER_RIGHT:
        m_xoff = m_dispWidth / 2;
        m_yoff = m_dispHeight / 2;
        break;
    }
}

std::list<QUADRANT> ILI_TFT::GetQuadrants()
{
    return quadrantList;
}

void ILI_TFT::setRotation(uint16_t screenWidth, uint16_t screenHeight, ROTATION rotation)
{
    switch (rotation)
    {
    case R0DEG:
        m_madctl |= MADCTL_MX;
        m_dispWidth  = screenWidth;
        m_dispHeight = screenHeight;
        break;
    case R90DEG:
        m_madctl |= MADCTL_MV;
        m_dispWidth  = screenHeight;
        m_dispHeight = screenWidth;
        break;
    case R180DEG:
        m_madctl |= MADCTL_MY;
        m_dispWidth  = screenWidth;
        m_dispHeight = screenHeight;
        break;
    case R270DEG:
        m_madctl |= (MADCTL_MY | MADCTL_MX | MADCTL_MV);
        m_dispWidth  = screenHeight;
        m_dispHeight = screenWidth;
        break;
    case MIRRORED0DEG:
        m_madctl |= MADCTL_MY | MADCTL_MX;
        m_dispWidth  = screenWidth;
        m_dispHeight = screenHeight;
        break;
    case MIRRORED90DEG:
        m_madctl |= (MADCTL_MX | MADCTL_MV);
        m_dispWidth  = screenHeight;
        m_dispHeight = screenWidth;
        break;
    case MIRRORED180DEG:
        m_dispWidth  = screenWidth;
        m_dispHeight = screenHeight;
        break;
    case MIRRORED270DEG:
        m_madctl |= (MADCTL_MY | MADCTL_MV);
        m_dispWidth  = screenHeight;
        m_dispHeight = screenWidth;
        break;
    }
}

void ILI_TFT::createFramebuf()
{
    ePixelFormat eFormat = DISPLAY_COLOUR_FORMAT;
    switch (m_nQuadrants)
    {
    case 1:
        Framebuf::Initialize(m_dispWidth, m_dispHeight, eFormat, bReverseBytes);
        quadrantList = {FULL_FRAME};
        break;
    case 2:
        if (m_dispWidth > m_dispHeight)
        {
            Framebuf::Initialize(m_dispWidth / 2, m_dispHeight, eFormat, bReverseBytes);
            quadrantList = {LEFT_HALF, RIGHT_HALF};
        }
        else
        {
            Framebuf::Initialize(m_dispWidth, m_dispHeight / 2, eFormat, bReverseBytes);
            quadrantList = {UPPER_HALF, LOWER_HALF};
        }
        break;
    case 4:
        Framebuf::Initialize(m_dispWidth / 2, m_dispHeight / 2, eFormat, bReverseBytes);
        quadrantList = {UPPER_LEFT, LOWER_LEFT, UPPER_RIGHT, LOWER_RIGHT};
        break;
    default:
        break;
    }
}

void ILI_TFT::SetPixel(int x, int y, uint16_t color)
{
    adjustPoint(x, y);
    return Framebuf::setpixel(x, y, color);
}

uint16_t ILI_TFT::GetPixel(int x, int y)
{
    adjustPoint(x, y);
    return Framebuf::getpixel(x, y);
}

void ILI_TFT::FillRect(int x, int y, int w, int h, uint16_t color)
{
    adjustPoint(x, y);
    return Framebuf::fillrect(x, y, w, h, color);
}

void ILI_TFT::Fill(uint16_t color)
{
    return Framebuf::fill(color);
}

void ILI_TFT::HLine(int x, int y, int w, uint16_t color)
{
    adjustPoint(x, y);
    return Framebuf::hline(x, y, w, color);
}

void ILI_TFT::VLine(int x, int y, int h, uint16_t color)
{
    adjustPoint(x, y);
    return Framebuf::vline(x, y, h, color);
}

void ILI_TFT::Rect(int x, int y, int w, int h, uint16_t color, bool bFill)
{
    adjustPoint(x, y);
    return Framebuf::rect(x, y, w, h, color, bFill);
}

void ILI_TFT::Line(int x1, int y1, int x2, int y2, uint16_t color)
{
    adjustPoint(x1, y1);
    adjustPoint(x2, y2);
    return Framebuf::line(x1, y1, x2, y2, color);
}

void ILI_TFT::Ellipse(int cx, int cy, int xradius, int yradius, uint16_t color, bool bFill, uint8_t mask)
{
    adjustPoint(cx, cy);
    return Framebuf::ellipse(cx, cy, xradius, yradius, color, bFill, mask);
}

void ILI_TFT::Text(const char* str, int x, int y, uint16_t color)
{
    adjustPoint(x, y);
    return Framebuf::text(str, x, y, color);
}

void ILI_TFT::Text(const char* str, int x, int y, uint16_t color, int scale)
{
    adjustPoint(x, y);
    return Framebuf::text(str, x, y, color, scale);
}

void ILI_TFT::Text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale)
{
    adjustPoint(x, y);
    return Framebuf::text(str, x, y, color, font, scale);
}

void ILI_TFT::Show()
{
    Show(0, 0, Framebuf::width(), Framebuf::height());
}

void ILI_TFT::Show(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t disp_x = x + m_xoff;
    uint16_t disp_y = y + m_yoff;

    uint16_t _x = MIN(Framebuf::width() - 1, MAX(0, x));
    uint16_t _y = MIN(Framebuf::height() - 1, MAX(0, y));
    uint16_t _w = MIN(Framebuf::width() - x, MAX(1, w));
    uint16_t _h = MIN(Framebuf::height() - y, MAX(1, h));

    uint8_t* pSrcData8 = reinterpret_cast<uint8_t*>(Framebuf::buffer());
    if (pSrcData8 == nullptr)
    {
        std::cout << "Framebuf::buffer() is nullptr, nothing to show" << std::endl;
        return;
    }
    uint16_t fWidth = Framebuf::width(); // framebuf width

    // This is the simplest, gets ~15fps.
    // writeBlock(_x, _y, _x + _w - 1, _y + _h - 1);
    // for (uint16_t iy = _y; iy < _y + _h; ++iy) // Draw line by line
    // {
    //     data(&pSrcData8[(iy * fWidth * 2) + _x], _w * 2);
    // }

    // This is more complicated, gets ~19fps for RGB565 while supporting RGB666.
    static uint8_t tgtBuffer[_MAX_CHUNK_SIZE * sizeof(uint16_t)];
    size_t bytesPerPixel = Framebuf::pixelSize();
    if (bytesPerPixel == 0)
    {
        return;
    }
    size_t bytesPerLine     = static_cast<size_t>(_w) * bytesPerPixel;
    size_t chunkBufferBytes = sizeof(tgtBuffer);

    // Fallback path if a single line does not fit in the staging buffer.
    if (bytesPerLine > chunkBufferBytes)
    {
        writeBlock(disp_x, disp_y, disp_x + _w - 1, disp_y + _h - 1);
        for (uint16_t iy = 0; iy < _h; ++iy)
        {
            size_t nSrcOffset = (static_cast<size_t>(_y + iy) * fWidth * bytesPerPixel) + (static_cast<size_t>(_x) * bytesPerPixel);
            sendFramebufferData(&pSrcData8[nSrcOffset], bytesPerLine);
        }
        return;
    }

    uint16_t linesPerChunk = static_cast<uint16_t>(chunkBufferBytes / bytesPerLine);
    if (linesPerChunk == 0)
    {
        linesPerChunk = 1;
    }
    uint16_t numChunks     = _h / linesPerChunk;
    uint16_t linesLeftover = _h - numChunks * linesPerChunk;

    // Tell the display where we are going to write the data
    writeBlock(disp_x, disp_y, disp_x + _w - 1, disp_y + _h - 1);
    for (uint16_t nChunk = 0; nChunk < numChunks; ++nChunk)
    {
        for (uint16_t iy = 0; iy < linesPerChunk; ++iy)
        {
            size_t nSrcOffset = (static_cast<size_t>(_y + (iy + nChunk * linesPerChunk)) * fWidth * bytesPerPixel) +
                                (static_cast<size_t>(_x) * bytesPerPixel);
            memcpy(tgtBuffer + (static_cast<size_t>(iy) * bytesPerLine), &pSrcData8[nSrcOffset], bytesPerLine);
        }
        sendFramebufferData(tgtBuffer, static_cast<size_t>(linesPerChunk) * bytesPerLine);
    }
    // Leftover lines
    for (uint16_t iy = 0; iy < linesLeftover; ++iy)
    {
        size_t nSrcOffset = (static_cast<size_t>(_y + (iy + numChunks * linesPerChunk)) * fWidth * bytesPerPixel) +
                            (static_cast<size_t>(_x) * bytesPerPixel);
        memcpy(tgtBuffer + (static_cast<size_t>(iy) * bytesPerLine), &pSrcData8[nSrcOffset], bytesPerLine);
    }
    sendFramebufferData(tgtBuffer, static_cast<size_t>(linesLeftover) * bytesPerLine);
}

void ILI_TFT::writeByte(uint8_t data)
{
    spi_write_blocking(m_spi, &data, 1);
}

void ILI_TFT::writeCmd(uint8_t cmd, uint8_t* data, size_t dataLen)
{
    cs_select();
    command_select();

    // spi write
    uint8_t commandBuffer[1];
    commandBuffer[0] = cmd;

    while (!spi_is_writable(m_spi))
    {
        sleep_us(1);
    }

    spi_write_blocking(m_spi, commandBuffer, 1);

    cs_deselect();

    // Write the data if any. Some commands won't take properly if we blast it all
    // at once so send individual bytes (this method is not used for framebuffer data,
    // which is sent in chunks).
    if (data != NULL)
    {
        for (size_t i = 0; i < dataLen; ++i)
        {
            sendData(data[i]);
        }
    }
}

void ILI_TFT::sendData(uint8_t* data, size_t dataLen)
{
    cs_select();
    data_select();

    spi_write_blocking(m_spi, data, dataLen);

    cs_deselect();
}

void ILI_TFT::sendFramebufferData(uint8_t* data, size_t dataLen)
{
    sendData(data, dataLen);
}

void ILI_TFT::writeBlock(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t* data, size_t dataLen)
{
    uint16_t buffer[2];
    uint8_t* pBuffer = reinterpret_cast<uint8_t*>(buffer);

    buffer[0] = __builtin_bswap16(x0);
    buffer[1] = __builtin_bswap16(x1);

    writeCmd(_CASET, pBuffer, 4);

    buffer[0] = __builtin_bswap16(y0);
    buffer[1] = __builtin_bswap16(y1);

    writeCmd(_PASET, pBuffer, 4);

    writeCmd(_RAMWR);
    sendData(data, dataLen);
}
