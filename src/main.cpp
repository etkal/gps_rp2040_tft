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
#include <limits>

#include "pico/stdlib.h"

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif

#include "gps_uart.h"
#include "gps_tft.h"
#include "timemgr.h"
#include "powermgr.h"
#include "log.h"

#if defined(GPS_ON_CORE_1) && defined(DISPLAY_ON_CORE_1)
#error "GPS_ON_CORE_1 and DISPLAY_ON_CORE_1 cannot both be defined"
#endif

#define UART0_DEVICE uart0 // Default is uart0
#define PIN_UART0_TX 0     // Default is 0
#define PIN_UART0_RX 1     // Default is 1

#if defined(ECHO_TO_UART1) && !defined(PICO_DEBUGPROBE)
#if defined(WAVESHARE_RP2040_ZERO)
#define UART1_DEVICE uart1 // uart1 for echo
#define PIN_UART1_TX 4
#define PIN_UART1_RX 5
#elif defined(PLATFORM_PICO)
#define UART1_DEVICE uart1 // uart1 for echo
#define PIN_UART1_TX 8
#define PIN_UART1_RX 9
#endif
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

#if !defined(DISPLAY_SPI_SPEED)
#define DISPLAY_SPI_SPEED 20000000 // 20MHz
#endif

// #define USE_WS2812_PIN 16 // Override
// #define USE_LED_PIN 16    // Override

// GPIO pin for a button
#define PIN_BUTTON 6

namespace
{
    constexpr uint64_t timeSyncRetryIntervalSec = 5 * 60;
} // namespace

#if !defined(NDEBUG)
// Used in debug builds to check for memory leaks
#include <malloc.h>
static uint32_t getTotalHeap()
{
    extern char __StackLimit, __bss_end__;
    return &__StackLimit - &__bss_end__;
}
static uint32_t getFreeHeap()
{
    struct mallinfo m = mallinfo();
    return getTotalHeap() - m.uordblks;
}
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

int main()
{
#if !defined(PICO_DEBUGPROBE)
    stdio_usb_init();
#else
    stdio_init_all(); // Use this for debugprobe
#endif

#if !defined(NDEBUG)
    timer_hw->dbgpause = 0;
    sleep_ms(5000);
#else
    sleep_ms(1000);
#endif

#if defined(PLATFORM_PICO_W)
    if (cyw43_arch_init())
    {
        std::cout << "Failed to initialize cyw43 hardware" << std::endl;
        return 1;
    }
#endif

    TimeMgr::InitializeSingleton(TIME_ZONE);

#if defined(DISPLAY_VSYS_VOLTAGE)
#if defined(ADC_GPIO_PIN)
    PowerMgr::InitializeSingleton(ADC_GPIO_PIN); // Initialize the power manager with the specified ADC GPIO pin
#elif defined(PICO_VSYS_PIN)
    PowerMgr::InitializeSingleton(PICO_VSYS_PIN); // Initialize the power manager with the VSYS pin
#endif
#endif // DISPLAY_VSYS_VOLTAGE
    auto spPowerMgr = PowerMgr::GetInstance();

    LogInfo("Starting GPS TFT application...");

#if defined(SEEED_XIAO_RP2040)
    // Clear LED(s) on XIAO (default on)
    LED_pico ledBlue(25);  // blue
    LED_pico ledGreen(16); // green
    LED_pico ledRed(17);   // red
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

    // Create the button object
    Button::Shared spButton;
#if defined(PIN_BUTTON)
    spButton = std::make_shared<Button>(PIN_BUTTON);
    spButton->Initialize();
#endif

    LogInfo("Creating GPS objects...");

    // Create the GPS object
    GPS_UART::Shared spGPS = std::make_shared<GPS_UART>();
    spGPS->SetInputUART(UART0_DEVICE, PIN_UART0_TX, PIN_UART0_RX, DATA_BITS, STOP_BITS, PARITY, UART_BAUD_RATE);
#if defined(UART1_DEVICE)
    spGPS->SetOutputUART(UART1_DEVICE, PIN_UART1_TX, PIN_UART1_RX, DATA_BITS, STOP_BITS, PARITY, UART_BAUD_RATE);
#endif

    LogInfo("Creating display objects...");
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
    // Create the GPS_TFT display object
    GPS_TFT::Shared spDevice = std::make_shared<GPS_TFT>(spDisplay, spGPS, spButton);

    // Start the GPS acquisition, might be local or on core 1
    spGPS->Start();
    // Start the GPS_TFT device
    spDevice->Start();

    uint64_t nLastTimeSyncAttemptSec = std::numeric_limits<uint64_t>::max();
    GPS_Status deviceStatus;
    uint64_t prevNowSecond = TimeMgr::CurrentEpochSeconds();

    while (true)
    {
        // Set the LED state based on GPS position or other criteria
        if (spLED)
        {
            spLED->DoWork(); // Handle any outstanding work (e.g. turn off blink)
        }

        spGPS->DoWork(); // Process the GPS

        spDevice->DoWork(); // Process the GPS_TFT and display (noop if it is autonomous on core 1)

        // Check if the device has received new data, limits the frequency of time synchronization attempts, etc.
        if (spDevice->GetStatus(deviceStatus))
        {
            // Update the system time if necessary
            if (!deviceStatus.strGpsTimeRaw.empty() && !deviceStatus.strGpsDateRaw.empty())
            {
                const uint64_t uptimeSec = time_us_64() / 1000000;
                const bool bNeverRetried = (nLastTimeSyncAttemptSec == std::numeric_limits<uint64_t>::max());
                const bool bUpdateDue = !TimeMgr::IsWallClockValid() || bNeverRetried ||
                                        (uptimeSec - nLastTimeSyncAttemptSec >= timeSyncRetryIntervalSec); // ||
                // !TimeMgr::IsGpsTimeDateWithinOneSecond(strGPSTimeRaw, strGPSDateRaw);
                if (bUpdateDue)
                {
                    nLastTimeSyncAttemptSec = uptimeSec;
                    LogInfo("Attempting GPS time sync");
                    if (TimeMgr::SetTimeFromGps(deviceStatus.strGpsTimeRaw, deviceStatus.strGpsDateRaw))
                    {
                        LogInfo("GPS time synchronized");
                    }
                }
            }
        }

        // Perform actions you want to occur once per second
        uint64_t nowSecond = TimeMgr::CurrentEpochSeconds();
        if (nowSecond != prevNowSecond)
        {
            prevNowSecond = nowSecond;

            // Set or blink the LED here based on the device status.
            if (spLED)
            {
                uint64_t nowSecond = TimeMgr::CurrentEpochSeconds();
                if (nowSecond != prevNowSecond)
                {
                    prevNowSecond = nowSecond;

                    if (deviceStatus.strGpsTimeRaw.empty())
                    {
                        spLED->SetPixel(0, led_red);
                        spLED->Blink_ms(0, 500);
                    }
                    else
                    {
                        if (deviceStatus.bHasPosition)
                        {
                            spLED->SetPixel(0, deviceStatus.bExternalAntenna ? led_blue : led_green);
                        }
                        else
                        {
                            spLED->SetPixel(0, deviceStatus.bExternalAntenna ? led_magenta : led_red);
                        }
                        spLED->Blink_ms(0, 50);
                    }
                }
            }

            // If configured, check the power source voltage and status.
            if (spPowerMgr)
            {
                auto fVoltage = spPowerMgr->GetVoltage(true);
                auto bUsingBattery = spPowerMgr->UsingBattery();

                if (0.0f != fVoltage)
                {
                    LogInfoD("Battery Voltage: " + std::to_string(fVoltage) + "  Using Battery: " + std::to_string(bUsingBattery));
                }
            }

#if !defined(NDEBUG)
            LogInfo("Total Heap: " + std::to_string(getTotalHeap()) + "  Free Heap: " + std::to_string(getFreeHeap()));
#endif
        }
    }

#if defined(PLATFORM_PICO_W)
    cyw43_arch_deinit();
#endif

    LogInfo("Exiting...");
    return 0;
}
