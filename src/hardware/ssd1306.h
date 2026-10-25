/*
 * Pico SSD1306 OLED driver class
 *
 * (c) 2023 Erik Tkal
 *
 * Based on a MicroPython implementation, combined with pico sample code
 *
 */

#pragma once

#include <memory>

#include <hardware/i2c.h>

#include "display.h"
#include "font.h"

// Register definitions
#define OLED_RDDSDR              0x0f // Read Display Self-Diagnostic Result
#define OLED_SLPOUT              0x11 // Sleep Out
#define OLED_GAMSET              0x26 // Gamma Set

#define OLED_SET_CONTRAST        0x81
#define OLED_SET_ENTIRE_ON       0xA4
#define OLED_SET_NORM_INV        0xA6
#define OLED_SET_DISP            0xAE
#define OLED_SET_MEM_ADDR        0x20
#define OLED_SET_COL_ADDR        0x21
#define OLED_SET_PAGE_ADDR       0x22
#define OLED_SET_DISP_START_LINE 0x40
#define OLED_SET_SEG_REMAP       0xA0
#define OLED_SET_MUX_RATIO       0xA8
#define OLED_SET_COM_OUT_DIR     0xC0
#define OLED_SET_DISP_OFFSET     0xD3
#define OLED_SET_COM_PIN_CFG     0xDA
#define OLED_SET_DISP_CLK_DIV    0xD5
#define OLED_SET_PRECHARGE       0xD9
#define OLED_SET_VCOM_DESEL      0xDB
#define OLED_SET_CHARGE_PUMP     0x8D
#define OLED_SET_SCROLL          0x2E

#define OLED_ADDR                0x3C
#define OLED_HEIGHT              64
#define OLED_WIDTH               128
#define OLED_PAGE_HEIGHT         8
#define OLED_NUM_PAGES           (OLED_HEIGHT / OLED_PAGE_HEIGHT)
#define OLED_BUF_LEN             (OLED_NUM_PAGES * OLED_WIDTH)

#define OLED_WRITE_MODE          0xFE
#define OLED_READ_MODE           0xFF

class SSD1306 : public Display
{
public:
    typedef std::shared_ptr<SSD1306> Shared;

    SSD1306(uint nWidth, uint nHeight, bool bExternalVcc);
    virtual ~SSD1306() = default;

    void Initialize() override;
    void Show() override;
    uint RecommendedFontSize() const override
    {
        return 12;
    }

    void DisplayOff();
    void DisplayOn();
    void SetContrast(uint8_t contrast);
    void Invert(bool bInvert);
    void Rotate(bool bRotate);

private:
    virtual void initInternal() = 0;
    virtual void write_cmd(uint8_t cmd) = 0;
    virtual void write_data(uint8_t* buf, uint nLen) = 0;

    bool m_bExternalVcc;
    uint m_nPages;
};

class SSD1306_I2C : public SSD1306
{
public:
    SSD1306_I2C(uint nWidth,
                uint nHeight,
                i2c_inst_t* i2c,
                uint sdaPin,
                uint sclPin,
                uint baudrate = 400 * 1000,
                uint8_t addr = 0x3C,
                bool bExternalVcc = false);
    virtual ~SSD1306_I2C() = default;

private:
    void initInternal() override;
    void write_cmd(uint8_t cmd) override;
    void write_data(uint8_t* buf, uint nLen) override;

    i2c_inst_t* m_i2c;
    uint m_sdaPin;
    uint m_sclPin;
    uint m_baudrate;
    uint8_t m_addr;
};
