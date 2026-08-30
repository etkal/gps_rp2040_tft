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
#include "power_status.h"
#include "font_factory.h"

#if !defined(NDEBUG)
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

#define SAT_ICON_RADIUS 4

namespace
{
    constexpr uint64_t timeSyncRetryIntervalSec = 5 * 60;
    constexpr double pi = 3.14159265359;
} // namespace

GPS_TFT::GPS_TFT(ILI_TFT::Shared spDisplay, GPS::Shared spGPS, LED::Shared spLED)
    : m_spDisplay(spDisplay),
      m_spGPS(spGPS),
      m_spLED(spLED),
      m_nLastTimeSyncAttemptSec(std::numeric_limits<uint64_t>::max())
{
    critical_section_init(&m_GpsDataCallbackCS);
}

GPS_TFT::~GPS_TFT()
{
}

void GPS_TFT::Initialize()
{
    m_spDisplay->Reset();
    m_spDisplay->Initialize();
    for (auto nQuadrant : m_spDisplay->GetQuadrants())
    {
        m_spDisplay->SetQuadrant(nQuadrant);
        m_spDisplay->Fill(COLOUR_BLACK);
        m_spDisplay->Show();
    }

    auto nFontSize = m_spDisplay->get_recommended_font_size();

    // Initialize display
    m_spDisplay->SetFont(get_recommended_font(nFontSize));

    showWaitingForGPS();

    m_spGPS->SetGpsDataCallback(this, gpsDataCB);
}

void GPS_TFT::Run()
{
    // Start GPS processing loop on processor core 1
    static auto sm_spGPS = m_spGPS; // Capture shared pointer for use in lambda
    multicore_launch_core1([]() {
        GPS::Shared spGPS = sm_spGPS;
        // Initialize and run the GPS processing loop on core 1
        LogInfo("Starting GPS processing on core 1");
        spGPS->Initialize();
        spGPS->Run();
    });

    m_spIdleTimer = std::make_shared<AlarmTimer>([this]() {
        LogInfo("GPS_TFT - No GPS data received showing waiting message");
        showWaitingForGPS();
    });

    // Main loop for updating the display
    while (true)
    {
        busy_wait_ms(10); // Sleep for 10ms to avoid busy waiting

        bool bHasQueuedGpsData = false;
        GPSData::Shared spGPSData;
        // Check if we have new GPS data to display, just take the most recent one and discard the rest to avoid UI lag
        critical_section_enter_blocking(&m_GpsDataCallbackCS);
        if (!m_qGPSData.empty())
        {
            bHasQueuedGpsData = true;
            while (!m_qGPSData.empty())
            {
                spGPSData = m_qGPSData.front();
                m_qGPSData.pop();
            }
        }
        critical_section_exit(&m_GpsDataCallbackCS);

        if (bHasQueuedGpsData && spGPSData)
        {
            LogInfo("GPS_TFT - Updating UI");
            updateUI(spGPSData);
            spGPSData.reset(); // Free the data
            m_spIdleTimer->Start(10000); // Reset the idle timer to 10 seconds
        }
    }
}

void GPS_TFT::gpsDataCB(void* pCtx, GPSData::Shared spGPSData)
{
    LogInfo("GPS_TFT - received GPS data");
    // This callback is called from the GPS processing loop when new GPS data is available.
    // It most likely runs on a different thread/core than the main display loop, so we need
    // to ensure thread safety.  We will perform a deep copy of the GPSData and then call
    // updateUI() on the main thread in the run loop as soon as queued data is available.
    GPS_TFT* pThis = reinterpret_cast<GPS_TFT*>(pCtx);
    if (nullptr == pThis)
    {
        LogInfo("gpsDataCB: pCtx is null");
        return;
    }

    // Make a deep copy of the GPSData to avoid issues with shared ownership and data races
    critical_section_enter_blocking(&pThis->m_GpsDataCallbackCS);
    GPSData::Shared spGPSDataCopy = std::make_shared<GPSData>(*spGPSData);
    pThis->m_qGPSData.push(spGPSDataCopy);
    critical_section_exit(&pThis->m_GpsDataCallbackCS);
}

void GPS_TFT::showWaitingForGPS()
{
    m_spDisplay->Clear(COLOUR_BLACK);
    auto nQuadrant = m_spDisplay->GetQuadrants().front();
    m_spDisplay->SetQuadrant(nQuadrant);
    drawText(0, "Waiting for GPS", COLOUR_RED, false, 0);
    m_spDisplay->Show();
}

// Update the UI with the latest GPS data.
void GPS_TFT::updateUI(GPSData::Shared spGPSData)
{
    m_spGPSData = spGPSData;
    if (m_spLED)
    {
        if (m_spGPSData->bHasPosition)
        {
            m_spLED->SetPixel(0, m_spGPSData->bExternalAntenna ? led_blue : led_green);
        }
        else
        {
            m_spLED->SetPixel(0, m_spGPSData->bExternalAntenna ? led_magenta : led_red);
        }
        m_spLED->Blink_ms(20);
    }

    // Update the system time if necessary
    if (!m_spGPSData->strGPSTimeRaw.empty() && !m_spGPSData->strGPSDateRaw.empty())
    {
        const uint64_t uptimeSec = time_us_64() / 1000000;
        const bool bNeverRetried = (m_nLastTimeSyncAttemptSec == std::numeric_limits<uint64_t>::max());
        const bool bUpdateDue = !TimeMgr::IsWallClockValid() || bNeverRetried ||
                                (uptimeSec - m_nLastTimeSyncAttemptSec >= timeSyncRetryIntervalSec) ||
                                !TimeMgr::IsGpsTimeDateWithinOneSecond(m_spGPSData->strGPSTimeRaw, m_spGPSData->strGPSDateRaw);
        if (bUpdateDue)
        {
            m_nLastTimeSyncAttemptSec = uptimeSec;
            LogInfo("Attempting GPS time sync");
            if (TimeMgr::SetTimeFromGps(m_spGPSData->strGPSTimeRaw, m_spGPSData->strGPSDateRaw))
            {
                LogInfo("GPS time synchronized");
            }
            else
            {
                LogInfo("GPS time sync failed");
            }
        }
    }

    uint16_t nWidth = m_spDisplay->Width();
    uint16_t nHeight = m_spDisplay->Height();

    // Compute padding dynamically from font dimensions
    constexpr uint PAD_CHARS_X = 1;
    constexpr uint PAD_CHARS_Y = 1;
    uint X_PAD = PAD_CHARS_X * getCharWidth();
    uint Y_PAD = PAD_CHARS_Y * getCharHeight();

#if defined(PLATFORM_PICO) // Only the Raspberry Pi Pico has a VSYS voltage monitor
    float vsys = 0.0;
    bool bBattery = false;
    std::string strVsys;
    if (PICO_OK == power_voltage(&vsys))
    {
        power_source(&bBattery);
        vsys = floorf(vsys * 100) / 100;
        std::stringstream oss;
        oss << (bBattery ? "batt: " : "vsys: ") << std::fixed << std::setfill(' ') << std::setprecision(1) << vsys << "v";
        strVsys = oss.str();
    }
#endif

    auto startTime = time_us_64();
    static uint64_t showTime = 0;

    for (auto nQuadrant : m_spDisplay->GetQuadrants())
    {
        m_spDisplay->SetQuadrant(nQuadrant);
        m_spDisplay->Fill(COLOUR_BLACK);

        // Draw the satellite map on the left half of the screen
        // Draw satellite grid, 3 rings
        if (m_spDisplay->Landscape())
        {
            drawSatGrid(nWidth / 4 + X_PAD, nHeight / 2, nWidth / 4 - X_PAD / 2, 3);
        }
        else
        {
            drawSatGrid(nWidth / 3 + X_PAD, nHeight / 2, nWidth / 3 - X_PAD / 2, 3);
        }

        // Draw fix and #sats text
        drawText(0, spGPSData->strMode3D + (m_spGPSData->bExternalAntenna ? "*" : ""), COLOUR_WHITE, false, X_PAD);
        drawText(3, spGPSData->strNumSats, COLOUR_WHITE, true, X_PAD);

        if (!spGPSData->strLatitude.empty())
        {
            drawText(0, spGPSData->strLatitude, COLOUR_WHITE, true, X_PAD);
            drawText(1, spGPSData->strLongitude, COLOUR_WHITE, true, X_PAD);
            drawText(2, spGPSData->strAltitude, COLOUR_WHITE, true, X_PAD);
            drawText(4, spGPSData->strSpeed, COLOUR_WHITE, true, X_PAD);
        }
        if (!spGPSData->strGPSTime.empty())
        {
            drawText(5, spGPSData->strGPSTime, COLOUR_WHITE, true, X_PAD);
        }

#if defined(PLATFORM_PICO)
        if (!strVsys.empty())
        {
            drawText(6, strVsys, COLOUR_WHITE, true, X_PAD);
        }
#endif

        // Draw clock
        if (TimeMgr::IsWallClockValid())
        {
            uint lineHeight = getCharHeight() + 1;
            uint radius = m_spDisplay->ShorterSide() / 8;
            uint xPos = m_spDisplay->Landscape() ? nWidth / 2 : X_PAD + getCharWidth() * 3;
            drawClock(xPos, lineHeight * PAD_CHARS_Y, radius, TimeMgr::FormatCurrentTimeHMS());
        }

        // Draw bar graph
        if (m_spGPSData->mSatList.size() > 0)
        {
            if (m_spDisplay->Landscape())
            {
                drawBarGraph(nWidth / 2, nHeight * 2 / 3, nWidth / 2 - X_PAD, nHeight / 3 - Y_PAD);
            }
            else
            {
                drawBarGraph(nWidth / 4, nHeight * 3 / 4, 3 * nWidth / 4 - X_PAD, nHeight / 4 - Y_PAD);
            }
        }

#if !defined(NDEBUG)
        drawText(7, "Show: " + std::to_string(showTime / 1000) + "ms", COLOUR_WHITE, true, X_PAD);
        drawText(8, "Free: " + std::to_string(getFreeHeap() / 1000) + "kB", COLOUR_WHITE, true, X_PAD);
#endif

        // blit the framebuf to the display quadrant
        m_spDisplay->Show();
    }
    showTime = time_us_64() - startTime;
    m_spGPSData.reset();

    LogInfo("Frame show: " + std::to_string(showTime / 1000) + "ms");
#if !defined(NDEBUG)
    LogInfo("Total Heap: " + std::to_string(getTotalHeap()) + "  Free Heap: " + std::to_string(getFreeHeap()));
#endif
}

void GPS_TFT::drawSatGrid(uint xCenter, uint yCenter, uint radius, uint nRings)
{
    for (uint i = 1; i <= nRings; ++i)
    {
        m_spDisplay->Ellipse(xCenter, yCenter, radius * i / nRings, radius * i / nRings, COLOUR_WHITE);
    }

    m_spDisplay->VLine(xCenter, yCenter - radius - 2, 2 * radius + 5, COLOUR_WHITE);
    m_spDisplay->HLine(xCenter - radius - 2, yCenter, 2 * radius + 5, COLOUR_WHITE);
    m_spDisplay->Text("N", xCenter - getCharWidth() / 2, yCenter - radius - getCharHeight(), COLOUR_RED);

    // m_spDisplay->Text("'", xCenter-6, yCenter-radius-CHAR_HEIGHT/2, COLOUR_RED);
    // m_spDisplay->Text("`", xCenter-2, yCenter-radius-CHAR_HEIGHT/2, COLOUR_RED);

    int satRadius = SAT_ICON_RADIUS / 2;
    if (!m_spGPSData->strLatitude.empty())
    {
        satRadius = SAT_ICON_RADIUS;
    }
    for (auto oEntry : m_spGPSData->mSatList)
    {
        auto oSat = oEntry.second;
        double elrad = oSat.m_el * pi / 180;
        double azrad = oSat.m_az * pi / 180;
        drawCircleSat(xCenter, yCenter, radius, elrad, azrad, satRadius, COLOUR_WHITE, COLOUR_BLACK);
        for (auto nSat : m_spGPSData->vUsedList)
        {
            if (oSat.m_num == nSat)
            {
                drawCircleSat(xCenter, yCenter, radius, elrad, azrad, satRadius, COLOUR_WHITE, COLOUR_BLUE);
                break;
            }
        }
    }
}

void GPS_TFT::drawCircleSat(uint gridCenterX,
                            uint gridCenterY,
                            uint nGridRadius,
                            float elrad,
                            float azrad,
                            uint satRadius,
                            uint16_t color,
                            uint16_t fillColor)
{
    // Draw satellite (fill first, then draw open circle)
    int dx = (nGridRadius - SAT_ICON_RADIUS) * cos(elrad) * sin(azrad);
    int dy = (nGridRadius - SAT_ICON_RADIUS) * cos(elrad) * -cos(azrad);
    int x = gridCenterX + dx;
    int y = gridCenterY + dy;
    m_spDisplay->Ellipse(x, y, satRadius, satRadius, fillColor, true); // Clear area with fill
    m_spDisplay->Ellipse(x, y, satRadius, satRadius, color);           // Draw circle without fill
}

void GPS_TFT::drawBarGraph(uint x, uint y, uint width, uint height)
{
    const BitmapFont* pFont = get_recommended_font(12); // 6x12, narrowest font for bar graph

    uint nMaxSats = 16;
    bool bNarrow = (nMaxSats * (2 * (pFont->width + 4)) > width);
    uint charWidth = bNarrow ? pFont->width : getCharWidth();
    uint charHeight = bNarrow ? pFont->height : getCharHeight();
    uint barDelta = bNarrow ? std::max(std::min(charWidth * 2 + 4, width / nMaxSats), charWidth * 2)
                            : std::max(std::min(charWidth + 4, width / nMaxSats), charWidth);
    uint barWidth = barDelta - 2;
    uint barPosX = x + width - (m_spGPSData->mSatList.size() * barDelta);
    uint barHeightMax = height - (charHeight + 1) * 2;

    for (auto oEntry : m_spGPSData->mSatList)
    {
        auto oSat = oEntry.second;
        uint rssi = oSat.m_rssi;
        uint barHeight = (int)((double)barHeightMax * (double)rssi / 64);
        uint baseLineY = y + barHeightMax;
        m_spDisplay->HLine(barPosX, baseLineY, barDelta, COLOUR_WHITE);
        uint nSat = oSat.m_num;

        std::stringstream oss;
        oss << std::fixed << std::setw(2) << std::setfill('0') << std::setprecision(0) << nSat;
        std::string strSatNum = oss.str();

        uint charPosX = bNarrow ? barPosX + (barDelta - (2 * charWidth)) / 2 : barPosX + (barDelta - charWidth) / 2;
        if (bNarrow)
        {
            m_spDisplay->Text(strSatNum.c_str(), charPosX, baseLineY + 2, COLOUR_WHITE, *pFont);
        }
        else
        {
            m_spDisplay->Text(strSatNum.substr(0, 1).c_str(), charPosX, baseLineY + 2, COLOUR_WHITE);
            m_spDisplay->Text(strSatNum.substr(1, 1).c_str(), charPosX, baseLineY + charHeight, COLOUR_WHITE);
        }
        if (barHeight > 0)
        {
            m_spDisplay->Rect(barPosX + 1, baseLineY - barHeight + 1, barWidth, barHeight, COLOUR_WHITE);
        }
        for (auto nSat : m_spGPSData->vUsedList)
        {
            if (oSat.m_num == nSat)
            {
                if (barHeight > 0)
                {
                    // draw inner filled rectangle to indicate used satellite
                    m_spDisplay->Rect(barPosX + 2, baseLineY - barHeight + 2, barWidth - 2, barHeight - 2, COLOUR_BLUE, true);
                }
                break;
            }
        }
        barPosX += barDelta;
    }
}

void GPS_TFT::drawClock(uint x, uint y, uint radius, std::string strTime)
{
    uint xCenter = x + radius;
    uint yCenter = y + radius;
    uint nHour = atoi(strTime.substr(0, 2).c_str());
    const float gmtOffset = TimeMgr::TimeZoneOffsetHours();
    float hour = (float)(nHour % 12) + gmtOffset;
    hour = (hour < 0) ? hour + 12 : hour;
    hour = (hour >= 12) ? hour - 12 : hour;
    float minute = (float)atoi(strTime.substr(3, 2).c_str());
    float second = (float)atoi(strTime.substr(6, 2).c_str());
    uint16_t ringColor = COLOUR_LIME;
    uint16_t faceColor = COLOUR_BLACK;
    uint16_t handColor = COLOUR_WHITE;
    uint16_t secondHandColor = COLOUR_RED;
    double handLenHour = radius * 0.4;
    double handLenMinute = radius * 0.7;
    double handLenSecond = radius * 0.8;
    double radiansHour = 2 * pi * (((hour * 3600.0) + (minute * 60.0) + second) / (12.0 * 60.0 * 60.0));
    double radiansMinute = 2 * pi * (((minute * 60.0) + second) / (60.0 * 60.0));
    double radiansSecond = 2 * pi * (second / 60.0);
    int dxh = int(handLenHour * sin(radiansHour));
    int dyh = int(handLenHour * -cos(radiansHour));
    int dxm = int(handLenMinute * sin(radiansMinute));
    int dym = int(handLenMinute * -cos(radiansMinute));
    int dxs = int(handLenSecond * sin(radiansSecond));
    int dys = int(handLenSecond * -cos(radiansSecond));

    // Draw the face
    m_spDisplay->Ellipse(xCenter, yCenter, radius, radius, ringColor, false);
    m_spDisplay->Ellipse(xCenter, yCenter, radius - 1, radius - 1, faceColor, true);
    // Draw quarter dots
    for (uint degDot = 0; degDot < 360; degDot += 30)
    {
        uint16_t colDot = (degDot % 90 == 0) ? COLOUR_BLUE : COLOUR_GRAY;
        uint16_t sizDot = (degDot % 90 == 0) ? 2 : 1;
        uint dxDot = int((radius - sizDot) * sin(degDot * pi / 180));
        uint dyDot = int((radius - sizDot) * -cos(degDot * pi / 180));
        m_spDisplay->Ellipse(xCenter + dxDot, yCenter + dyDot, sizDot, sizDot, colDot, true);
    }
    // Draw the hands
    m_spDisplay->Line(xCenter, yCenter, xCenter + dxs, yCenter + dys, secondHandColor);
    m_spDisplay->Line(xCenter, yCenter, xCenter + dxh, yCenter + dyh, handColor);
    m_spDisplay->Line(xCenter, yCenter, xCenter + dxm, yCenter + dym, handColor);
    // m_spDisplay->ellipse(xCenter, yCenter, 1, 1, faceColor, true);
}

int GPS_TFT::linePos(int nLine)
{
    constexpr uint PAD_CHARS_Y = 1;
    if (nLine >= 0)
        return (nLine + PAD_CHARS_Y) * getLineAdvance();
    else
        return m_spDisplay->Height() + (nLine * getLineAdvance());
}

void GPS_TFT::drawText(int nLine, std::string strText, uint16_t color, bool bRightAlign, uint nPadding)
{
    uint charWidth = getCharWidth();
    int x = (!bRightAlign) ? 0 : m_spDisplay->Width() - (strText.length() * charWidth);
    int y = linePos(nLine);
    x += bRightAlign ? -nPadding : nPadding;
    m_spDisplay->Text(strText.c_str(), x, y, color);
}
