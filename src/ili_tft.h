/*
 * Pico ILI TFT display driver class
 *
 * Currently supports ILI9341 and ILI9488 displays.
 *
 * (c) 2024-2026 Erik Tkal
 *
 * Modified from Darren Horrocks ILI934X version to fix command/data/select timing,
 * as well as removing the GFXFont support.
 * Modified to inherit from Framebuffer (itself a version modified from analyzing
 * the Damien P. George MicroPython modframebuf.c).
 *
 * Notes:
 *   1) To use framebuf, set portrait mode, i.e. width = 240, height = 320 for ILI934X,
 *      width = 320, height = 480 for ILI948X, and then rotate appropriately, e.g. 270 degrees.
 *      Let the framebuf return its width and height for content calculation purposes.
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

#pragma once

#include <list>
#include <memory>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "framebuf.h"
#include "font.h"

// ILI TFT commands
//
#define _RDDSDR         0x0f // Read Display Self-Diagnostic Result
#define _SLPOUT         0x11 // Sleep Out
#define _GAMSET         0x26 // Gamma Set
#define _DISPOFF        0x28 // Display Off
#define _DISPON         0x29 // Display On
#define _CASET          0x2a // Column Address Set
#define _PASET          0x2b // Page Address Set
#define _RAMWR          0x2c // Memory Write
#define _RAMRD          0x2e // Memory Read
#define _MADCTL         0x36 // Memory Access Control
#define _VSCRSADD       0x37 // Vertical Scrolling Start Address
#define _COLMOD         0x3a // Pixel Color Mode Set
#define _PWCTRLA        0xcb // Power Control A
#define _PWCRTLB        0xcf // Power Control B
#define _DTCTRLA        0xe8 // Driver Timing Control A
#define _DTCTRLB        0xea // Driver Timing Control B
#define _PWRONCTRL      0xed // Power on Sequence Control
#define _PRCTRL         0xf7 // Pump Ratio Control
#define _PWCTRL1        0xc0 // Power Control 1
#define _PWCTRL2        0xc1 // Power Control 2
#define _PWCTRL3        0xc2 // Power Control 3
#define _VMCTRL1        0xc5 // VCOM Control 1
#define _VMCTRL2        0xc7 // VCOM Control 2
#define _FRMCTR1        0xb1 // Frame Rate Control 1
#define _DISCTRL        0xb6 // Display Function Control
#define _ENA3G          0xf2 // Enable 3G
#define _PGAMCTRL       0xe0 // Positive Gamma Control
#define _NGAMCTRL       0xe1 // Negative Gamma Control
#define _DSPINVON       0x21 // Display Inversion On
#define _DSPINVOFF      0x20 // Display Inversion Off
#define _CSCON          0xF0 // Command Set Control for ST7796

#define MADCTL_MY       0x80 ///< Bottom to top
#define MADCTL_MX       0x40 ///< Right to left
#define MADCTL_MV       0x20 ///< Reverse Mode
#define MADCTL_ML       0x10 ///< LCD refresh Bottom to top
#define MADCTL_MH       0x04 ///< LCD refresh right to left

#define MADCTL_RGB      0x00 ///< Red-Green-Blue pixel order
#define MADCTL_BGR      0x08 ///< Blue-Green-Red pixel order

#define _MAX_CHUNK_SIZE 4096

// Windows 16-bit colour pallet converted to 5-6-5
#define COLOUR_BLACK     0x0000
#define COLOUR_MAROON    0x8000
#define COLOUR_GREEN     0x0400
#define COLOUR_OLIVE     0x8400
#define COLOUR_NAVY      0x0010
#define COLOUR_PURPLE    0x8010
#define COLOUR_TEAL      0x0410
#define COLOUR_SILVER    0xC618
#define COLOUR_GRAY      0x8410
#define COLOUR_RED       0xF800
#define COLOUR_LIME      0x07E0
#define COLOUR_YELLOW    0xFFE0
#define COLOUR_BLUE      0x001F
#define COLOUR_FUCHSIA   0xF81F
#define COLOUR_AQUA      0x07FF
#define COLOUR_WHITE     0xFFFF

#define COLOUR_ORDER_RGB MADCTL_RGB
#define COLOUR_ORDER_BGR MADCTL_BGR

#if !defined(DISPLAY_COLOUR_ORDER)
#define DISPLAY_COLOUR_ORDER COLOUR_ORDER_BGR
#endif

#define ILI934X_HW_WIDTH  240
#define ILI934X_HW_HEIGHT 320
#define ILI948X_HW_WIDTH  320
#define ILI948X_HW_HEIGHT 480
#define ST7796_HW_WIDTH   320
#define ST7796_HW_HEIGHT  480

enum ROTATION
{
    R0DEG,
    R90DEG,
    R180DEG,
    R270DEG,
    MIRRORED0DEG,
    MIRRORED90DEG,
    MIRRORED180DEG,
    MIRRORED270DEG
};

enum QUADRANT
{
    FULL_FRAME,
    LEFT_HALF,
    RIGHT_HALF,
    UPPER_HALF,
    LOWER_HALF,
    UPPER_LEFT,
    LOWER_LEFT,
    UPPER_RIGHT,
    LOWER_RIGHT
};

// Base ILI TFT class
//
class ILI_TFT : public Framebuf
{
public:
    typedef std::shared_ptr<ILI_TFT> Shared;

    ILI_TFT(spi_inst_t* spi, uint miso, uint mosi, uint sck, uint cs, uint dc, uint rst, uint bl, ROTATION rotation = R0DEG);
    virtual ~ILI_TFT() = default;

    virtual void Reset() = 0;
    virtual void Initialize();

    void Clear(uint16_t colour = COLOUR_BLACK); // Clear entire display via hardware access
    void SetQuadrant(QUADRANT eQuadrant);
    std::list<QUADRANT> GetQuadrants();
    void Show();
    void Show(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

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
    void Text(const char* str, int x, int y, uint16_t color, int scale);
    void Text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1);

    static inline uint16_t Colour565(uint8_t r, uint8_t g, uint8_t b)
    {
        return (((r >> 3) & 0x1f) << 11) | (((g >> 2) & 0x3f) << 5) | ((b >> 3) & 0x1f);
    }

    uint16_t Width() const
    {
        return m_dispWidth;
    }

    uint16_t Height() const
    {
        return m_dispHeight;
    }

    bool Landscape() const
    {
        return Width() >= Height();
    }
    bool Portrait() const
    {
        return !Landscape();
    }
    uint16_t ShorterSide() const
    {
        return (m_dispWidth < m_dispHeight) ? m_dispWidth : m_dispHeight;
    }

    int get_recommended_font_size() const
    {
        auto shorterSide = ShorterSide();
        if (shorterSide >= 320)
            return 18;
        if (shorterSide >= 240)
            return 14;
        return 12;
    }

protected:
    void createFramebuf();
    void setRotation(uint16_t screenWidth, uint16_t screenHeight, ROTATION rotation = R0DEG);
    void adjustPoint(int& x, int& y)
    {
        x -= m_xoff;
        y -= m_yoff;
    }

    virtual void sendData(uint8_t data) = 0;

    void writeByte(uint8_t data);
    void writeCmd(uint8_t cmd, uint8_t* data = NULL, size_t dataLen = 0);
    void sendData(uint8_t* data, size_t dataLen = 0);
    virtual void sendFramebufferData(uint8_t* data, size_t dataLen = 0);
    void writeBlock(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t* data = NULL, size_t dataLen = 0);

    inline void cs_select()
    {
        gpio_put(m_cs, 0); // Active low
    }
    inline void cs_deselect()
    {
        gpio_put(m_cs, 1);
    }
    inline void command_select()
    {
        gpio_put(m_dc, 0);
    }
    inline void data_select()
    {
        gpio_put(m_dc, 1);
    }

protected:
    // hardware pins
    spi_inst_t* m_spi = NULL;
    uint m_miso;
    uint m_mosi;
    uint m_sck;
    uint m_cs;
    uint m_dc;
    uint m_rst;
    uint m_bl;

    // display properties
    uint16_t m_dispWidth;
    uint16_t m_dispHeight;
    ROTATION m_rotation;
    uint8_t m_madctl;
    uint8_t m_colmod;
    uint16_t m_nQuadrants;
    std::list<QUADRANT> quadrantList;
    QUADRANT m_eQuadrant;
    uint16_t m_xoff;
    uint16_t m_yoff;
};

// ILI934X-specific TFT class
//
#if defined(DISPLAY_ILI934X)
class ILI934X : public ILI_TFT
{
public:
    ILI934X(spi_inst_t* spi, uint miso, uint mosi, uint sck, uint cs, uint dc, uint rst, uint bl, ROTATION rotation = R0DEG);
    virtual ~ILI934X() = default;

    void Initialize() override;
    void Reset() override;

private:
    void sendData(uint8_t data) override;
    void sendFramebufferData(uint8_t* data, size_t dataLen = 0) override;
};
#endif // DISPLAY_ILI934X

// ILI948X-specific TFT class
//
#if defined(DISPLAY_ILI948X)
class ILI948X : public ILI_TFT
{
public:
    ILI948X(spi_inst_t* spi, uint miso, uint mosi, uint sck, uint cs, uint dc, uint rst, uint bl, ROTATION rotation = R0DEG);
    virtual ~ILI948X() = default;

    void Initialize() override;
    void Reset() override;

private:
    void sendData(uint8_t data) override;
    void sendFramebufferData(uint8_t* data, size_t dataLen = 0) override;
};
#endif // DISPLAY_ILI948X

// ST7796-specific TFT class
//
#if defined(DISPLAY_ST7796)
class ST7796 : public ILI_TFT
{
public:
    ST7796(spi_inst_t* spi, uint miso, uint mosi, uint sck, uint cs, uint dc, uint rst, uint bl, ROTATION rotation = R0DEG);
    virtual ~ST7796() = default;

    void Initialize() override;
    void Reset() override;

private:
    void sendData(uint8_t data) override;
    void sendFramebufferData(uint8_t* data, size_t dataLen = 0) override;
};
#endif // DISPLAY_ST7796
