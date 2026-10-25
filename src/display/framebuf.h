/*
 * Framebuf class
 *
 * (c) 2024-2026 Erik Tkal
 *
 */

#pragma once

#include <memory>
#include <cstdint>

#include "pico/stdlib.h"
#include "font.h"

// Windows 16-bit colour palette converted to RGB565
enum eColour : uint16_t
{
    COLOUR_BLACK = 0x0000,
    COLOUR_MAROON = 0x8000,
    COLOUR_GREEN = 0x0400,
    COLOUR_OLIVE = 0x8400,
    COLOUR_NAVY = 0x0010,
    COLOUR_PURPLE = 0x8010,
    COLOUR_TEAL = 0x0410,
    COLOUR_SILVER = 0xC618,
    COLOUR_GRAY = 0x8410,
    COLOUR_RED = 0xF800,
    COLOUR_LIME = 0x07E0,
    COLOUR_YELLOW = 0xFFE0,
    COLOUR_BLUE = 0x001F,
    COLOUR_FUCHSIA = 0xF81F,
    COLOUR_AQUA = 0x07FF,
    COLOUR_WHITE = 0xFFFF,
};

#if !defined(DISPLAY_COLOUR_FORMAT)
#define DISPLAY_COLOUR_FORMAT RGB565
#endif

typedef enum ePixelFormat
{
    MVLSB,         // ssd1306
    RGB565,        // ili934x / ili948x / st7796
    MHLSB,         // unused
    MHMSB,         // unused
    RGB666,        // 18-bit pixel format (3 bytes per pixel) - required for some displays like ili9488
    RGB565_BYTE,   // 1 byte per pixel palette index (16 colours), sent to display as RGB565
    RGB666_BYTE,   // 1 byte per pixel palette index (16 colours), sent to display as RGB666
    RGB565_NIBBLE, // 4 bits per pixel palette index (2 pixels per byte, even x in high nibble), sent as RGB565
    RGB666_NIBBLE, // 4 bits per pixel palette index (2 pixels per byte, even x in high nibble), sent as RGB666
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

    void SetPixel(int x, int y, uint16_t color);
    uint16_t GetPixel(int x, int y);
    void FillRect(int x, int y, int w, int h, uint16_t color);
    void Fill(uint16_t color);
    void HLine(int x, int y, int w, uint16_t color);
    void VLine(int x, int y, int h, uint16_t color);
    void Rect(int x, int y, int w, int h, uint16_t color, bool bFill = false);
    void Line(int x1, int y1, int x2, int y2, uint16_t color);
    void Ellipse(int cx, int cy, int xradius, int yradius, uint16_t color, bool bFill = false, uint8_t mask = ELLIPSE_MASK_ALL);
    void Text(const char* str, int x, int y, uint16_t color);
    // Draw text with integer scaling factor (1 = normal size)
    void Text(const char* str, int x, int y, uint16_t color, int scale);
    // Draw text using a bitmap font
    void Text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1);

    // Set the default font for text() calls (nullptr to use hardcoded font_petme128_8x8)
    inline void SetFont(const BitmapFont* pFont)
    {
        m_pFont = pFont;
    }
    inline const BitmapFont* GetFont() const
    {
        return m_pFont;
    }
    inline void ClearFont()
    {
        m_pFont = nullptr;
    }

protected:
    inline void SetOffset(uint16_t xoff, uint16_t yoff)
    {
        m_xoff = xoff;
        m_yoff = yoff;
    }
    inline uint16_t GetXOffset()
    {
        return m_xoff;
    }
    inline uint16_t GetYOffset()
    {
        return m_yoff;
    }
    inline void* buffer()
    {
        return m_pBuf;
    }
    inline uint16_t width()
    {
        return m_nWidth;
    }
    inline uint16_t height()
    {
        return m_nHeight;
    }
    // Bytes per pixel as sent to the display
    inline uint16_t pixelSize()
    {
        return m_nPixelSize;
    }
    // Bytes per pixel as stored in the framebuffer (differs from pixelSize() for the _BYTE formats)
    inline uint16_t storedPixelSize()
    {
        return m_nStoredSize;
    }
    inline bool isPalettized()
    {
        return RGB565_BYTE == m_eFormat || RGB666_BYTE == m_eFormat || isNibble();
    }
    inline bool isNibble()
    {
        return RGB565_NIBBLE == m_eFormat || RGB666_NIBBLE == m_eFormat;
    }
    // Convert n palette-indexed pixels starting at pixel index nFirst of the framebuffer to display
    // format using the lookup table
    void expandPixels(const uint8_t* base, size_t nFirst, uint8_t* dst, size_t n);

private:
    bool check(int& x, int& y);
    bool check(int& x, int& y, int& h, int& w);

    // The following methods should only ever call the private method versions so as to
    // only adjust for the offsets once per external call.
    void setPixel(int x, int y, uint16_t color);
    uint16_t getPixel(int x, int y);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void text(const char* str, int x, int y, uint16_t color);
    // Draw text with integer scaling factor (1 = normal size)
    void text(const char* str, int x, int y, uint16_t color, int scale);
    // Draw text using a bitmap font
    void text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1);
    void ellipse_points(int cx, int cy, int x, int y, uint16_t color, uint8_t mask);
    void setpixel_masked(int x, int y, uint16_t color, uint8_t mask);

    inline uint8_t getNibble(size_t index) const
    {
        uint8_t v = ((const uint8_t*)m_pBuf)[index >> 1];
        return (index & 1) ? (v & 0x0f) : (v >> 4);
    }
    inline void setNibble(size_t index, uint8_t value)
    {
        uint8_t& v = ((uint8_t*)m_pBuf)[index >> 1];
        v = (index & 1) ? ((v & 0xf0) | value) : ((v & 0x0f) | (value << 4));
    }

    void scroll(int xstep, int ystep);

    inline uint16_t fixcolor(uint16_t color)
    {
        return m_bRevBytes ? __builtin_bswap16(color) : color;
    }
    inline void adjustPoint(int& x, int& y)
    {
        x -= m_xoff;
        y -= m_yoff;
    }

    void* m_pBuf {};
    uint16_t m_xoff {};
    uint16_t m_yoff {};
    uint16_t m_nPixelSize {};
    uint16_t m_nStoredSize {};
    uint8_t m_lut[16][3] {}; // display-format bytes for each palette index
    uint16_t m_nWidth {};
    uint16_t m_nHeight {};
    uint16_t m_nStride {};
    ePixelFormat m_eFormat {};
    bool m_bRevBytes {};
    const BitmapFont* m_pFont {};
};
