/*
 * Copyright (c) 2025-2026 Erik Tkal
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

#include <iostream>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif

#include "gps_uart.h"
#include "gps_tft.h"
#include "font_factory.h"
#include "timemgr.h"

#define UART0_DEVICE uart0                    // Default is uart0
#define PIN_UART0_TX PICO_DEFAULT_UART_TX_PIN // Default is 0
#define PIN_UART0_RX PICO_DEFAULT_UART_RX_PIN // Default is 1

#if defined(WAVESHARE_RP2040_ZERO) || defined(PLATFORM_PICO)
#define UART1_DEVICE uart1 // uart1 for echo
#define PIN_UART1_TX 4
#define PIN_UART1_RX 5
#endif

#define UART_BAUD_RATE 9600
#define DATA_BITS      8
#define STOP_BITS      1
#define PARITY         UART_PARITY_NONE

#if defined(DISPLAY_PICO_RESTOUCH) // Waveshare Pico-ResTouch-LCD-3.5
#define SPI_DEVICE spi1
#define PIN_DC     8
#define PIN_CS     9
#define PIN_SCK    10
#define PIN_MOSI   11
#define PIN_MISO   12
#define PIN_BL     13
#define PIN_RST    15
#elif defined(PLATFORM_PICO)                // Pico, Pico W, Pico 2, Pico 2 W
#define SPI_DEVICE spi_default              // Default is SPI0 for Pico
#define PIN_MISO   PICO_DEFAULT_SPI_RX_PIN  // White  16
#define PIN_CS     PICO_DEFAULT_SPI_CSN_PIN // Org    17
#define PIN_SCK    PICO_DEFAULT_SPI_SCK_PIN // Purple 18
#define PIN_MOSI   PICO_DEFAULT_SPI_TX_PIN  // Blue   19
#define PIN_RST    20                       // Yellow
#define PIN_DC     21                       // Green
#define PIN_BL     22                       // Gray
#elif defined(SEEED_XIAO_RP2040)
// XIAO has spi0 CSn overlap with uart0, so override
#define SPI_DEVICE spi_default              // Default is SPI0 for XIAO
#define PIN_MISO   PICO_DEFAULT_SPI_RX_PIN  // White  4
#define PIN_CS     26                       // Orange
#define PIN_SCK    PICO_DEFAULT_SPI_SCK_PIN // Purple 2
#define PIN_MOSI   PICO_DEFAULT_SPI_TX_PIN  // Blue   3
#define PIN_RST    27                       // Yellow
#define PIN_DC     28                       // Green
#define PIN_BL     29                       // Gray
#elif defined(WAVESHARE_RP2040_ZERO)
// RP2040-Zero has default spi1, which is on the bottom, so use spi0
#define SPI_DEVICE spi0 // override
#define PIN_MISO   4    // White
#define PIN_CS     5    // Org
#define PIN_SCK    6    // Purple
#define PIN_MOSI   7    // Blue
#define PIN_RST    14   // Yellow
#define PIN_DC     15   // Green
#define PIN_BL     29   // Gray
#else
#error unknown board
#endif

// #define USE_WS2812_PIN 12 // Override
// #define USE_LED_PIN 16    // Override

#if !defined(DISPLAY_SPI_SPEED)
#define DISPLAY_SPI_SPEED 20000000 // 20MHz
#endif

extern "C"
{
    int _getentropy(void* buffer, size_t length)
    {
        (void)buffer;
        (void)length;
        return ENOSYS;
    }
}

#if !defined(NDEBUG)
void SplashDemo(ILI_TFT::Shared spDisplay);
#endif

int main()
{
    stdio_usb_init();
    adc_init();

#if !defined(NDEBUG)
    sleep_ms(5000);
#endif

    TimeMgr::InitializeSingleton(TIME_ZONE); // Needed for logging timestamps
    LogInfo("Starting GPS TFT application...");

#if defined(SEEED_XIAO_RP2040)
    // Clear LED(s) on XIAO (default on)
    LED_pico ledBlue(25);  // blue
    LED_pico ledGreen(16); // green
    LED_pico ledRed(17);   // red
#endif

#if defined(PLATFORM_PICO_W)
    cyw43_arch_init();
#endif

    // Create the LED object
    LED::Shared spLED;
#if defined(USE_WS2812_PIN)
    spLED = std::make_shared<LED_neo>(1, USE_WS2812_PIN);
    spLED->Initialize();
    spLED->SetPixel(0, led_green);
#elif defined(PICO_DEFAULT_WS2812_PIN) && !defined(USE_LED_PIN)
    spLED = std::make_shared<LED_neo>(1, PICO_DEFAULT_WS2812_PIN);
    spLED->Initialize();
    spLED->SetPixel(0, led_green);
#elif defined(USE_LED_PIN)
    spLED = std::make_shared<LED_pico>(USE_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#elif defined(PICO_DEFAULT_LED_PIN)
    spLED = std::make_shared<LED_pico>(PICO_DEFAULT_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#elif defined(PLATFORM_PICO_W)
    spLED = std::make_shared<LED_pico_w>(CYW43_WL_GPIO_LED_PIN);
    spLED->Initialize();
    spLED->SetIgnore({led_red, led_magenta});
#endif

    LogInfo("Creating GPS object...");

    // Create the GPS object
    GPS_UART::Shared spGPS = std::make_shared<GPS_UART>();
    spGPS->SetInputUART(UART0_DEVICE, PIN_UART0_TX, PIN_UART0_RX, DATA_BITS, STOP_BITS, PARITY, UART_BAUD_RATE);
#if defined(UART1_DEVICE)
    spGPS->SetOutputUART(UART1_DEVICE, PIN_UART1_TX, PIN_UART1_RX, DATA_BITS, STOP_BITS, PARITY, UART_BAUD_RATE);
#endif

    LogInfo("Creating display object...");
    // Create the display. ILI9341 or ILI9488, rotate 270 degrees for landscape.
#if defined(DISPLAY_ILI934X)
    ILI934X::Shared spDisplay =
        std::make_shared<ILI934X>(SPI_DEVICE, PIN_MISO, PIN_MOSI, PIN_SCK, PIN_CS, PIN_DC, PIN_RST, PIN_BL, DISPLAY_ROTATION);
#elif defined(DISPLAY_ILI948X)
    ILI948X::Shared spDisplay =
        std::make_shared<ILI948X>(SPI_DEVICE, PIN_MISO, PIN_MOSI, PIN_SCK, PIN_CS, PIN_DC, PIN_RST, PIN_BL, DISPLAY_ROTATION);
#elif defined(DISPLAY_ST7796)
    ST7796::Shared spDisplay =
        std::make_shared<ST7796>(SPI_DEVICE, PIN_MISO, PIN_MOSI, PIN_SCK, PIN_CS, PIN_DC, PIN_RST, PIN_BL, DISPLAY_ROTATION);
#else
#error Unsupported display specified
#endif

    LogInfo("Initializing display object...");
    spDisplay->Initialize();
    LogInfo("Clearing display...");
    spDisplay->Clear(COLOUR_BLACK);

#if !defined(NDEBUG)
    LogInfo("Showing splash demo...");
    SplashDemo(spDisplay);
    spDisplay->Clear(COLOUR_BLACK);
#endif

    // Create the GPS_TFT display object
    GPS_TFT::Shared spDevice = std::make_shared<GPS_TFT>(spDisplay, spGPS, spLED);

    spDevice->Initialize();

    // Run the show
    spDevice->Run();

#if defined(PLATFORM_PICO_W)
    cyw43_arch_deinit();
#endif

    LogInfo("Exiting...");
    return 0;
}

#if !defined(NDEBUG)
void SplashDemo(ILI_TFT::Shared spDisplay)
{
    // Palette demo splash: show all 16 named RGB565 colors with labels
    struct NamedColour
    {
        const char* name;
        const char* hex;
        uint16_t value;
    };

    static const NamedColour colours[16] = {
        {"BLACK",   "0x0000", COLOUR_BLACK  },
        {"MAROON",  "0x8000", COLOUR_MAROON },
        {"GREEN",   "0x0400", COLOUR_GREEN  },
        {"OLIVE",   "0x8400", COLOUR_OLIVE  },
        {"NAVY",    "0x0010", COLOUR_NAVY   },
        {"PURPLE",  "0x8010", COLOUR_PURPLE },
        {"TEAL",    "0x0410", COLOUR_TEAL   },
        {"SILVER",  "0xC618", COLOUR_SILVER },
        {"GRAY",    "0x8410", COLOUR_GRAY   },
        {"RED",     "0xF800", COLOUR_RED    },
        {"LIME",    "0x07E0", COLOUR_LIME   },
        {"YELLOW",  "0xFFE0", COLOUR_YELLOW },
        {"BLUE",    "0x001F", COLOUR_BLUE   },
        {"FUCHSIA", "0xF81F", COLOUR_FUCHSIA},
        {"AQUA",    "0x07FF", COLOUR_AQUA   },
        {"WHITE",   "0xFFFF", COLOUR_WHITE  },
    };

    auto text_colour_for_bg = [](uint16_t c) -> uint16_t {
        uint8_t r5 = (c >> 11) & 0x1f;
        uint8_t g6 = (c >> 5) & 0x3f;
        uint8_t b5 = c & 0x1f;
        uint16_t r = (r5 * 255) / 31;
        uint16_t g = (g6 * 255) / 63;
        uint16_t b = (b5 * 255) / 31;
        uint16_t luma = static_cast<uint16_t>((299u * r + 587u * g + 114u * b) / 1000u);
        return (luma > 140) ? COLOUR_BLACK : COLOUR_WHITE;
    };

    const int cols = 4;
    const int rows = 4;
    int dispW = spDisplay->Width();
    int dispH = spDisplay->Height();
    int cellW = dispW / cols;
    int cellH = dispH / rows;

    auto nFontSize = spDisplay->get_recommended_font_size();
    // Initialize display
    spDisplay->SetFont(get_recommended_font(nFontSize));

    for (auto nQuadrant : spDisplay->GetQuadrants())
    {
        spDisplay->SetQuadrant(nQuadrant);
        spDisplay->Fill(COLOUR_BLACK);
        for (int i = 0; i < 16; ++i)
        {
            int col = i % cols;
            int row = i / cols;
            int x = col * cellW;
            int y = row * cellH;
            int w = (col == cols - 1) ? (dispW - x) : cellW;
            int h = (row == rows - 1) ? (dispH - y) : cellH;

            spDisplay->FillRect(x, y, w, h, colours[i].value);
            uint16_t textColour = text_colour_for_bg(colours[i].value);
            spDisplay->Text(colours[i].name, x + 3, y + 3, textColour);
            spDisplay->Text(colours[i].hex, x + 3, y + spDisplay->GetFont()->height + 3, textColour);
        }

        spDisplay->Show();
    }
    sleep_ms(2000);
    spDisplay->Clear(COLOUR_BLACK);
}
#endif
