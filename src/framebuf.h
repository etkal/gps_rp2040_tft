/*
 * Framebuf class
 *
 * (c) 2024-2026 Erik Tkal
 *
 */

#pragma once

#include <memory>

#include "pico/stdlib.h"
#include "font.h"

#if !defined(DISPLAY_COLOUR_FORMAT)
#define DISPLAY_COLOUR_FORMAT RGB565
#endif

typedef enum ePixelFormat
{
    MVLSB,  // ssd1306
    RGB565, // ili9341
    MHLSB,
    MHMSB,
    RGB666, // 18-bit pixel format (3 bytes per pixel)
} ePixelFormat;

struct pixel666
{
    inline pixel666& operator=(const pixel666& other)
    {
        b1 = other.b1;
        b2 = other.b2;
        b3 = other.b3;
        return *this;
    }

    inline pixel666& operator=(const uint16_t& pixel565)
    {
        uint8_t r = (pixel565 >> 11) & 0x1f;
        uint8_t g = (pixel565 >> 5) & 0x3f;
        uint8_t b = pixel565 & 0x1f;
        b1 = r << 3;
        b2 = g << 2;
        b3 = b << 3;
        return *this;
    }

    inline operator uint16_t() const
    {
        uint8_t r = b1 >> 3;
        uint8_t g = b2 >> 2;
        uint8_t b = b3 >> 3;
        return (r << 11) | (g << 5) | b;
    }

    uint8_t b1;
    uint8_t b2;
    uint8_t b3;
};

// Q2 Q1
// Q3 Q4
#define ELLIPSE_MASK_FILL (0x10)
#define ELLIPSE_MASK_ALL  (0x0f)
#define ELLIPSE_MASK_Q1   (0x01)
#define ELLIPSE_MASK_Q2   (0x02)
#define ELLIPSE_MASK_Q3   (0x04)
#define ELLIPSE_MASK_Q4   (0x08)

// So far these displays always use big-endian byte order
constexpr bool bReverseBytes = true;

class Framebuf
{
public:
    typedef std::shared_ptr<Framebuf> Shared;

    Framebuf();
    ~Framebuf();
    void Initialize(uint16_t nWidth, uint16_t nHeight, ePixelFormat eFormat, bool bRevBytes = false, uint16_t nStride = 0);

    void setpixel(int x, int y, uint16_t color);
    uint16_t getpixel(int x, int y);
    void fillrect(int x, int y, int w, int h, uint16_t color);

    void fill(uint16_t color);
    void hline(int x, int y, int w, uint16_t color);
    void vline(int x, int y, int h, uint16_t color);
    void rect(int x, int y, int w, int h, uint16_t color, bool bFill = false);
    void line(int x1, int y1, int x2, int y2, uint16_t color);
    void ellipse(int cx, int cy, int xradius, int yradius, uint16_t color, bool bFill = false, uint8_t mask = ELLIPSE_MASK_ALL);
    void text(const char* str, int x, int y, uint16_t color);
    // Draw text with integer scaling factor (1 = normal size)
    void text(const char* str, int x, int y, uint16_t color, int scale);
    // Draw text using a bitmap font
    void text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1);

    // Set the default font for text() calls (nullptr to use hardcoded font_petme128_8x8)
    void SetFont(const BitmapFont* pFont)
    {
        m_pFont = pFont;
    }
    const BitmapFont* GetFont() const
    {
        return m_pFont;
    }
    void ClearFont()
    {
        m_pFont = nullptr;
    }

    void* buffer()
    {
        return m_pBuf;
    }
    uint16_t width()
    {
        return m_nWidth;
    }
    uint16_t height()
    {
        return m_nHeight;
    }
    uint16_t pixelSize()
    {
        return m_nPixelSize;
    }

private:
    bool check(int& x, int& y);
    bool check(int& x, int& y, int& h, int& w);

    void ellipse_points(int cx, int cy, int x, int y, uint16_t color, uint8_t mask);
    void setpixel_masked(int x, int y, uint16_t color, uint8_t mask);

    void scroll(int xstep, int ystep);

    uint16_t fixcolor(uint16_t color)
    {
        return m_bRevBytes ? __builtin_bswap16(color) : color;
    }

    void* m_pBuf;
    uint16_t m_nPixelSize;
    uint16_t m_nWidth;
    uint16_t m_nHeight;
    uint16_t m_nStride;
    ePixelFormat m_eFormat;
    bool m_bRevBytes;
    const BitmapFont* m_pFont;
};
