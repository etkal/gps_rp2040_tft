/*
 * GPS using TFT display
 *
 * Copyright (c) 2024-2026 Erik Tkal
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

#include "gps_tft.h"

#include <stdio.h>
#include <string>
#include <iostream>
#include <math.h>
#include <iomanip>

#include "pico/stdlib.h"
#include "pico/double.h"
#include "pico/multicore.h"

#include "ili_tft.h"
#include "font_factory.h"
#include "log.h"
#include "timemgr.h"
#include "powermgr.h"

#define SAT_ICON_RADIUS 4

namespace
{
    constexpr double pi = 3.14159265359;
} // namespace

GPS_TFT::GPS_TFT(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton)
    : GPSDevice(spDisplay, spGPS, spButton)
{
    // Custom display attributes to be used by common code the GPSDevice implementation.
    // Defaults should be sufficient for most use cases.
    m_satIconRadius = 4;
    m_bClockThickHands = false;
    m_clockQuarterDotsColour = COLOUR_AQUA;
    m_clockRingColour = COLOUR_LIME;
}

GPS_TFT::~GPS_TFT()
{
}

bool GPS_TFT::handleButtonEvent()
{
    ButtonEvent eLastButtonEvent = getLastButtonEvent();
    if (ButtonEvent::None == eLastButtonEvent)
    {
        return false;
    }
    if (ButtonEvent::Tap == eLastButtonEvent)
    {
        LogInfoD("Button tap detected");
    }
    else if (ButtonEvent::Press == eLastButtonEvent)
    {
        LogInfoD("Button press detected");
    }
    else if (ButtonEvent::LongPress == eLastButtonEvent)
    {
        LogInfoD("Long button press detected");
    }

    switch (eLastButtonEvent)
    {
    case ButtonEvent::Tap:
        break;
    case ButtonEvent::Press:
        break;
    case ButtonEvent::LongPress:
        break;
    default:
        break;
    }

    return true; // trigger a display update on state change
}

void GPS_TFT::showScreenMessage(std::string strMessage)
{
    Clear();
    m_spDisplay->SetFont(get_recommended_font(24));
    m_spDisplay->DoQuadrants([this, &strMessage]() {
        drawText(4, std::string("    ") +strMessage, COLOUR_RED, false, 0);
    });
}

// Update the UI with the latest GPS data.
void GPS_TFT::updateUI(GPSData::Shared spGPSData)
{
    LogInfoD("GPS_TFT - updateUI() called");

#if !defined(NDEBUG)
    auto startTime = time_us_64();
    static uint64_t showTime = 0;
#endif

    // Reset the default font in case another screen (e.g. displayed message) changed it
    auto nFontSize = m_spDisplay->RecommendedFontSize();
    m_spDisplay->SetFont(get_recommended_font(nFontSize));
    uint16_t nWidth = m_spDisplay->Width();
    uint16_t nHeight = m_spDisplay->Height();

    // Compute padding dynamically from font dimensions
    constexpr uint PAD_CHARS_X = 1;
    constexpr uint PAD_CHARS_Y = 1;
    uint X_PAD = PAD_CHARS_X * getCharWidth();
    uint Y_PAD = PAD_CHARS_Y * getCharHeight();

    // Get battery voltage if available
    std::string strVoltage;
    auto spPowerMgr = PowerMgr::GetInstance();
    if (spPowerMgr)
    {
        strVoltage = spPowerMgr->GetStatusString();
    }

    m_spDisplay->DoQuadrants([this, spGPSData, nWidth, nHeight, X_PAD, Y_PAD, strVoltage]() {
        // Draw satellite grid, 3 rings
        if (m_spDisplay->Landscape())
        {
            drawSatGrid(spGPSData, nWidth / 4 + X_PAD, nHeight / 2, nWidth / 4 - X_PAD / 2, 3);
        }
        else
        {
            drawSatGrid(spGPSData, nWidth / 3 + X_PAD, nHeight / 2, nWidth / 3 - X_PAD / 2, 3);
        }

        // Draw textual information
        drawText(1, spGPSData->strMode3D + (spGPSData->bExternalAntenna ? "*" : ""), COLOUR_WHITE, false, X_PAD);

        drawText(1, spGPSData->strLatitude, COLOUR_WHITE, true, X_PAD);
        drawText(2, spGPSData->strLongitude, COLOUR_WHITE, true, X_PAD);
        drawText(3, spGPSData->strAltitude, COLOUR_WHITE, true, X_PAD);
        drawText(4, spGPSData->strSpeed, COLOUR_WHITE, true, X_PAD);

        drawText(5, spGPSData->strNumSats, COLOUR_WHITE, true, X_PAD);

        // Draw battery voltage if available
        drawText(6, strVoltage.c_str(), COLOUR_WHITE, true, X_PAD);

        if (!spGPSData->strGPSTime.empty())
        {
            drawText(7, spGPSData->strGPSTime, COLOUR_WHITE, true, X_PAD);
        }

        // Draw clock
        if (!spGPSData->strGPSTime.empty())
        {
            uint lineHeight = getCharHeight() + 1;
            uint radius = m_spDisplay->ShorterSide() / 8;
            uint xPos = m_spDisplay->Landscape() ? nWidth / 2 : X_PAD + getCharWidth() * 3;
            drawClock(xPos, lineHeight * PAD_CHARS_Y, radius, spGPSData->strGPSTime);
        }

        // Draw bar graph
        if (spGPSData->mSatList.size() > 0)
        {
            if (m_spDisplay->Landscape())
            {
                drawBarGraph(spGPSData, nWidth / 2, nHeight * 2 / 3, nWidth / 2 - X_PAD, nHeight / 3 - Y_PAD);
            }
            else
            {
                drawBarGraph(spGPSData, nWidth / 4, nHeight * 3 / 4, 3 * nWidth / 4 - X_PAD, nHeight / 4 - Y_PAD);
            }
        }
#if !defined(NDEBUG)
        drawText(9, "Show: " + std::to_string(showTime / 1000) + "ms", COLOUR_WHITE, true, X_PAD);
#endif
    });

#if !defined(NDEBUG)
    showTime = time_us_64() - startTime;
    LogInfo("Frame show: " + std::to_string(showTime / 1000) + "ms");
#endif
}

void GPS_TFT::showSplashScreen()
{
#if !defined(NDEBUG)
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
    int dispW = m_spDisplay->Width();
    int dispH = m_spDisplay->Height();
    int cellW = dispW / cols;
    int cellH = dispH / rows;

    auto nFontSize = m_spDisplay->RecommendedFontSize();
    m_spDisplay->SetFont(get_recommended_font(nFontSize));

    m_spDisplay->DoQuadrants([this, cols, rows, dispW, dispH, cellW, cellH, text_colour_for_bg]() {
        for (int i = 0; i < 16; ++i)
        {
            int col = i % cols;
            int row = i / cols;
            int x = col * cellW;
            int y = row * cellH;
            int w = (col == cols - 1) ? (dispW - x) : cellW;
            int h = (row == rows - 1) ? (dispH - y) : cellH;

            m_spDisplay->FillRect(x, y, w, h, colours[i].value);
            uint16_t textColour = text_colour_for_bg(colours[i].value);
            m_spDisplay->Text(colours[i].name, x + 3, y + 3, textColour);
            m_spDisplay->Text(colours[i].hex, x + 3, y + m_spDisplay->GetFont()->height + 3, textColour);
        }
    });
    sleep_ms(2000);
#endif
    SetFont(get_recommended_font(32));
    showScreenMessage("ET GPS");
    sleep_ms(2000);
}
