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

GPS_TFT::GPS_TFT(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton, Touch::Shared spTouch)
    : GPSDevice(spDisplay, spGPS, spButton, spTouch)
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

void GPS_TFT::Initialize()
{
    GPSDevice::Initialize();

    auto nFontSize = m_spDisplay->RecommendedFontSize();
    m_pBaseFont = get_recommended_font(nFontSize);
    m_spDisplay->SetFont(m_pBaseFont);

    uint nHeight = m_spDisplay->Height();
    uint nWidth = m_spDisplay->Width();
    uint nLineHeight = getLineAdvance();
    uint nCharWidth = getCharWidth();

    // Widget position and sizes for display and touch sensor areas
    //
    m_nClockRadius = m_spDisplay->ShorterSide() / 8;
    m_nClockX = m_spDisplay->Landscape() ? m_spDisplay->Width() / 2 + m_nClockRadius : nWidth / 3 + nCharWidth;
    m_nClockY = nLineHeight + m_nClockRadius;

    if (m_spDisplay->Landscape())
    {
        m_nSatGridX = nWidth / 4 + nCharWidth;
        m_nSatGridY = nHeight / 2;
        m_nSatGridRadius = nWidth / 4 - nCharWidth / 2;
    }
    else
    {
        m_nSatGridX = nWidth / 3 + nCharWidth;
        m_nSatGridY = nHeight / 2;
        m_nSatGridRadius = nWidth / 3 - nCharWidth / 2;
    }

    if (m_spDisplay->Landscape())
    {
        m_nBarGraphX = nWidth / 2;
        m_nBarGraphY = nHeight * 2 / 3;
        m_nBarGraphWidth = nWidth / 2 - nCharWidth;
        m_nBarGraphHeight = nHeight / 3 - nLineHeight;
    }
    else
    {
        m_nBarGraphX = nWidth / 4;
        m_nBarGraphY = nHeight * 3 / 4;
        m_nBarGraphWidth = 3 * nWidth / 4 - nCharWidth;
        m_nBarGraphHeight = nHeight / 4 - nLineHeight;
    }

    m_nTextAreaX = nWidth - 15 * nCharWidth;
    m_nTextAreaY = 0;
    m_nTextAreaWidth = 15 * nCharWidth - 1;
    m_nTextAreaHeight = 12 * nLineHeight - 1;

    m_nUpperLeftX = 0;
    m_nUpperLeftY = 0;
    m_nUpperLeftRadius = m_spDisplay->ShorterSide() / 4;
    m_nLowerLeftX = 0;
    m_nLowerLeftY = m_spDisplay->Height() - 1;
    m_nLowerLeftRadius = m_spDisplay->ShorterSide() / 4;
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

bool GPS_TFT::handleTouchEvent(const TouchEvent& event)
{
    // Treat a tap like a button tap, with the registered area ID available for finer control
    if (TouchEventType::Tap != event.eType)
    {
        return false;
    }
    LogInfo("Touch tap at " + std::to_string(event.x) + "," + std::to_string(event.y) +
            (event.bHasArea
                 ? " area id " + std::to_string(event.nAreaId) + " (" + TouchArea::GetName(static_cast<TouchArea::Id>(event.nAreaId)) + ")"
                 : std::string(" no registered area")));

    if (event.bHasArea)
    {
        static AlarmTimer echoTimer([this]() {
            m_bDisplayTapEcho = false;
        });
        m_nTappedAreaId = event.nAreaId;
        m_bDisplayTapEcho = true;
        echoTimer.Start(500); // Display the tap echo for 500 milliseconds minimum
        // Act on event.nAreaId, the ID given to spTouch->AddArea()
        switch (event.nAreaId)
        {
        case TouchArea::Clock:
            // Handle clock area tap
            break;
        case TouchArea::SatGrid:
            // Handle satellite grid area tap
            m_satIconRadius = ((m_satIconRadius - (m_satIconRadius % 2)) % 8) + 2;
            break;
        case TouchArea::BarGraph:
            // Handle bar graph area tap
            if (COLOUR_BLUE == m_barsColour)
            {
                m_barsColour = COLOUR_GREEN;
            }
            else
            {
                m_barsColour = COLOUR_BLUE;
            }
            break;
        case TouchArea::TextArea:
            // Handle text area tap
            m_bAltitudeFeet = !m_bAltitudeFeet;
            m_eSpeedUnit = (SpeedUnit::Kph == m_eSpeedUnit) ? SpeedUnit::Mph : SpeedUnit::Kph;
            break;
        default:
            break;
        }
        return true; // trigger a display update on state change
    }
    return false; // no display update needed if touch event was not in a registered area
}

void GPS_TFT::showScreenMessage(std::string strMessage)
{
    // Disable touch areas while showing a screen message
    auto spTouch = getTouch();
    if (spTouch)
    {
        spTouch->ClearAreas();
        m_bTouchAreasAdded = false;
    }
    Clear();
    m_spDisplay->SetFont(get_recommended_font(24));
    m_spDisplay->DoQuadrants([this, &strMessage]() {
        drawText(4, std::string("    ") + strMessage, COLOUR_RED, false, 0);
    });
}

// Register the touch areas for the clickable screen objects. The geometry here must parallel the
// calculations in updateUI() which draw the objects. Areas added first have priority where they overlap.
void GPS_TFT::addTouchAreasIfNeeded()
{
    if (m_bTouchAreasAdded)
    {
        return;
    }

    auto spTouch = getTouch();
    if (spTouch)
    {
        spTouch->AddCircleArea(TouchArea::Clock, m_nClockX, m_nClockY, m_nClockRadius);
        spTouch->AddCircleArea(TouchArea::SatGrid, m_nSatGridX, m_nSatGridY, m_nSatGridRadius);
        spTouch->AddArea(TouchArea::BarGraph, m_nBarGraphX, m_nBarGraphY, m_nBarGraphX + m_nBarGraphWidth, m_nBarGraphY + m_nBarGraphHeight);
        spTouch->AddArea(TouchArea::TextArea, m_nTextAreaX, m_nTextAreaY, m_nTextAreaX + m_nTextAreaWidth, m_nTextAreaY + m_nTextAreaHeight);
        spTouch->AddCircleArea(TouchArea::UpperLeft, m_nUpperLeftX, m_nUpperLeftY, m_nUpperLeftRadius);
        spTouch->AddCircleArea(TouchArea::LowerLeft, m_nLowerLeftX, m_nLowerLeftY, m_nLowerLeftRadius);
    }

    m_bTouchAreasAdded = true;
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

    addTouchAreasIfNeeded();
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

    bool bDisplayTapEcho = m_bDisplayTapEcho;
    m_spDisplay->DoQuadrants([this, spGPSData, nWidth, nHeight, X_PAD, Y_PAD, strVoltage, bDisplayTapEcho]() {
        // Draw satellite grid, 3 rings
        drawSatGrid(spGPSData, m_nSatGridX, m_nSatGridY, m_nSatGridRadius, 3);

        // Draw textual information
        drawText(1, spGPSData->strMode3D + (spGPSData->bExternalAntenna ? "*" : ""), COLOUR_WHITE, false, X_PAD);

        drawText(1, spGPSData->strLatitude, COLOUR_WHITE, true, X_PAD);
        drawText(2, spGPSData->strLongitude, COLOUR_WHITE, true, X_PAD);
        drawText(3, m_bAltitudeFeet ? spGPSData->strAltitudeFeet : spGPSData->strAltitude, COLOUR_WHITE, true, X_PAD);
        drawText(4, m_eSpeedUnit == SpeedUnit::Kph ? spGPSData->strSpeedKph : spGPSData->strSpeedMph, COLOUR_WHITE, true, X_PAD);

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
            drawClock(m_nClockX, m_nClockY, m_nClockRadius, spGPSData->strGPSTime);
        }

        // Draw bar graph
        if (spGPSData->mSatList.size() > 0)
        {
            drawBarGraph(spGPSData, m_nBarGraphX, m_nBarGraphY, m_nBarGraphWidth, m_nBarGraphHeight);
        }

        // Draw tap echo if enabled
        if (bDisplayTapEcho)
        {
            // uint nLineHeight = getLineAdvance();
            uint nCharWidth = getCharWidth();
            auto nColor = COLOUR_FUCHSIA;
            switch (static_cast<TouchArea::Id>(m_nTappedAreaId))
            {
            case TouchArea::Clock:
                Ellipse(m_nClockX, m_nClockY, m_nClockRadius + 3, m_nClockRadius + 3, nColor, false);
                Ellipse(m_nClockX, m_nClockY, m_nClockRadius + 2, m_nClockRadius + 2, nColor, false);
                break;
            case TouchArea::SatGrid:
                Ellipse(m_nSatGridX, m_nSatGridY, m_nSatGridRadius + 3, m_nSatGridRadius + 3, nColor, false);
                Ellipse(m_nSatGridX, m_nSatGridY, m_nSatGridRadius + 2, m_nSatGridRadius + 2, nColor, false);
                break;
            case TouchArea::TextArea:
                Rect(m_nTextAreaX + 1, m_nTextAreaY + 1, m_nTextAreaWidth - 2, m_nTextAreaHeight - 2, nColor, false);
                Rect(m_nTextAreaX, m_nTextAreaY, m_nTextAreaWidth, m_nTextAreaHeight, nColor, false);
                break;
            case TouchArea::BarGraph:
                Rect(m_nBarGraphX + 1, m_nBarGraphY + 1, m_nBarGraphWidth + nCharWidth - 3, m_nBarGraphHeight - 3, nColor, false);
                Rect(m_nBarGraphX, m_nBarGraphY, m_nBarGraphWidth + nCharWidth - 1, m_nBarGraphHeight - 1, nColor, false);
                break;
            case TouchArea::UpperLeft:
                Ellipse(m_nUpperLeftX, m_nUpperLeftY, m_nUpperLeftRadius + 1, m_nUpperLeftRadius + 1, nColor, false);
                Ellipse(m_nUpperLeftX, m_nUpperLeftY, m_nUpperLeftRadius, m_nUpperLeftRadius, nColor, false);
                break;
            case TouchArea::LowerLeft:
                Ellipse(m_nLowerLeftX, m_nLowerLeftY, m_nLowerLeftRadius + 1, m_nLowerLeftRadius + 1, nColor, false);
                Ellipse(m_nLowerLeftX, m_nLowerLeftY, m_nLowerLeftRadius, m_nLowerLeftRadius, nColor, false);
                break;
            default:
                drawText(8, "Tapped area: Unknown", COLOUR_WHITE, true, X_PAD);
                break;
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
#if defined(NDEBUG)
    constexpr int splashDelayMs = 1000; // Delay for splash screen
#else
    constexpr int splashDelayMs = 2000; // Longer delay in debug mode
#endif
    // Palette demo splash: show all 16 named RGB565 colors with labels
    struct NamedColour
    {
        const char* name;
        uint16_t value;
    };

    static const NamedColour colours[16] = {
        {"BLACK",   COLOUR_BLACK  },
        {"MAROON",  COLOUR_MAROON },
        {"GREEN",   COLOUR_GREEN  },
        {"OLIVE",   COLOUR_OLIVE  },
        {"NAVY",    COLOUR_NAVY   },
        {"PURPLE",  COLOUR_PURPLE },
        {"TEAL",    COLOUR_TEAL   },
        {"SILVER",  COLOUR_SILVER },
        {"GRAY",    COLOUR_GRAY   },
        {"RED",     COLOUR_RED    },
        {"LIME",    COLOUR_LIME   },
        {"YELLOW",  COLOUR_YELLOW },
        {"BLUE",    COLOUR_BLUE   },
        {"FUCHSIA", COLOUR_FUCHSIA},
        {"AQUA",    COLOUR_AQUA   },
        {"WHITE",   COLOUR_WHITE  },
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
            char hex[7];
            snprintf(hex, sizeof(hex), "0x%04X", static_cast<unsigned>(colours[i].value));
            m_spDisplay->Text(hex, x + 3, y + m_spDisplay->GetFont()->height + 3, textColour);
        }
    });

    // A touch holds the palette until the next touch
    auto spTouch = getTouch();
    if (spTouch)
    {
        TouchEvent event;
        spTouch->DoWork();
        while (spTouch->GetEvent(event))
        {
        } // discard stale events

        auto waitForTouch = [&](uint32_t timeoutMs) -> bool {
            absolute_time_t end = make_timeout_time_ms(timeoutMs);
            while (timeoutMs == 0 || !time_reached(end))
            {
                spTouch->DoWork();
                while (spTouch->GetEvent(event))
                {
                    if (event.eType == TouchEventType::Down)
                    {
                        return true;
                    }
                }
                sleep_ms(10);
            }
            return false;
        };

        if (waitForTouch(splashDelayMs))
        {
            sleep_ms(300); // let the finger lift so the same press isn't seen twice
            spTouch->DoWork();
            while (spTouch->GetEvent(event))
            {
            }
            waitForTouch(0);
        }
    }
    else
    {
        sleep_ms(splashDelayMs);
    }
    SetFont(get_recommended_font(32));
    showScreenMessage(PRODUCT_SPLASH_STRING);
    sleep_ms(splashDelayMs);
}
