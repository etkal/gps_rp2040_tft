/*
 * GPS using display
 *
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

#include "gpsdevice.h"

#include <iomanip>

#include "pico/stdlib.h"
#include "pico/double.h"
#include "pico/multicore.h"

#include "font_factory.h"
#include "log.h"
#include "timemgr.h"
#include "powermgr.h"

namespace
{
    constexpr double pi = 3.14159265359;
} // namespace

GPSDevice::GPSDevice(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton)
    : m_spDisplay(spDisplay),
      m_spGPS(spGPS),
      m_spButton(spButton)
{
    if (!m_spDisplay || !m_spGPS)
    {
        panic("GPSDevice constructor: One or more required pointers are null");
    }
    critical_section_init(&m_CallbackCs);
}

void GPSDevice::Initialize()
{
    queue_init(&m_qIncomingGPSData, sizeof(GPSData::Shared*), 10); // Initialize the queue with a capacity of 10

    m_spDisplay->Initialize();

    showSplashScreen();

    // Initialize display with desired font
    auto nFontSize = m_spDisplay->RecommendedFontSize();
    m_spDisplay->SetFont(get_recommended_font(nFontSize));

    m_bShowWaitingForGPS = true;

    m_spGPS->SetGpsDataCallback(this, gpsDataCB);
    m_spGPS->SetMessageCallback(this, messageCB);
    if (m_spButton)
    {
        m_spButton->SetEventCallback(this, buttonEventCB);
    }

    m_spIdleTimer = std::make_shared<AlarmTimer>([this]() {
        m_bShowWaitingForGPS = true;
    });

    m_bInitialized = true;
}

void GPSDevice::Start()
{
#if defined(DISPLAY_ON_CORE_1)
    static GPSDevice::Shared sm_spThis = shared_from_this(); // Capture pointer for use in lambda
    // Default core 1 stack is only 4KB; iostream/ostringstream logging overflows it
    static uint32_t sm_core1Stack[16 * 1024 / sizeof(uint32_t)];
    multicore_launch_core1_with_stack(
        []() {
            GPSDevice::Shared spThis = sm_spThis;
            // Initialize and run the GPS processing loop on core 1
            LogInfo("Starting GPSDevice processing on core 1");
            spThis->Initialize();
            spThis->Run();
        },
        sm_core1Stack,
        sizeof(sm_core1Stack));
#else
    LogInfo("Initializing GPSDevice processing on core 0");
    Initialize();
#endif
}

void GPSDevice::Run()
{
#if defined(DISPLAY_ON_CORE_1)
    while (!m_bExit)
    {
        DoWork();
    }
#else
    LogInfo("GPSDevice::Run() should not be called on core 0");
    return;
#endif
}

void GPSDevice::DoWork()
{
#if defined(DISPLAY_ON_CORE_1)
    if (0 == get_core_num())
    {
        return; // Skip processing on core 0 if GPSDevice is running on core 1
    }
#endif
    if (!m_bInitialized)
    {
        return;
    }
    if (!m_bExit)
    {
        if (handleButtonEvent() && m_spLastGPSData)
        {
            // Redraw immediately with the last known data rather than waiting for the next GPS update
            updateUI(m_spLastGPSData);
        }

        GPSData::Shared spGPSData = dequeueLatestGPSData(m_qIncomingGPSData);
        if (spGPSData)
        {
            LogInfoD("GPSDevice - Processing new GPS data");
            m_bShowWaitingForGPS = false;
            critical_section_enter_blocking(&m_CallbackCs);
            m_bStatusChanged = true;
            m_bHasPosition = spGPSData->bHasPosition;
            m_bExternalAntenna = spGPSData->bExternalAntenna;
            m_strGpsTimeRaw = spGPSData->strGPSTimeRaw;
            m_strGpsDateRaw = spGPSData->strGPSDateRaw;
            critical_section_exit(&m_CallbackCs);
            m_spLastGPSData = spGPSData;
            updateUI(spGPSData);
            m_spIdleTimer->Start(5000); // Reset the idle timer
        }
        if (m_bShowWaitingForGPS)
        {
            LogInfo("GPSDevice - No GPS data received showing waiting message");
            critical_section_enter_blocking(&m_CallbackCs);
            m_bStatusChanged = true;
            m_bHasPosition = false;
            m_bExternalAntenna = false;
            m_strGpsTimeRaw.clear();
            m_strGpsDateRaw.clear();
            critical_section_exit(&m_CallbackCs);
            showScreenMessage("Waiting for GPS data");
            m_bShowWaitingForGPS = false;
        }
    }
}

// Stop the GPSDevice processing loop. This will cause Run() to return.
void GPSDevice::Stop()
{
    m_bExit = true;
}

bool GPSDevice::GetStatus(GPS_Status& status)
{
    critical_section_enter_blocking(&m_CallbackCs);
    if (!m_bStatusChanged)
    {
        critical_section_exit(&m_CallbackCs);
        return false;
    }
    m_bStatusChanged = false;
    status.bHasPosition = m_bHasPosition;
    status.bExternalAntenna = m_bExternalAntenna;
    status.strGpsTimeRaw = m_strGpsTimeRaw;
    status.strGpsDateRaw = m_strGpsDateRaw;
    critical_section_exit(&m_CallbackCs);
    return true;
}

ButtonEvent GPSDevice::getLastButtonEvent()
{
    critical_section_enter_blocking(&m_CallbackCs);
    ButtonEvent eLast = m_eLastButtonEvent;
    m_eLastButtonEvent = ButtonEvent::None;
    critical_section_exit(&m_CallbackCs);
    return eLast;
}

//
// Static callbacks
//
void GPSDevice::gpsDataCB(void* pCtx, GPSData::Shared spGPSData)
{
    LogInfoD("GPSDevice - received GPS data");
    // This callback is called from the GPS processing loop when new GPS data is available.
    // It most likely runs on a different thread/core than the main display loop, so we need
    // to ensure thread safety.  We will perform a deep copy of the GPSData and then call
    // updateUI() on the main thread in the run loop as soon as queued data is available.
    GPSDevice* pThis = reinterpret_cast<GPSDevice*>(pCtx);
    if (nullptr == pThis)
    {
        LogInfo("gpsDataCB: pCtx is null");
        return;
    }

    // Deep copy the GPSData once here; all subsequent hand-offs share ownership of this copy.
    enqueueGPSData(pThis->m_qIncomingGPSData, std::make_shared<GPSData>(*spGPSData));
}

void GPSDevice::messageCB(void* pCtx, std::string strMessage)
{
    LogInfo("GPSDevice - received message: " + strMessage);
    GPSDevice* pThis = reinterpret_cast<GPSDevice*>(pCtx);
    if (nullptr == pThis)
    {
        LogInfo("messageCB: pCtx is null");
        return;
    }

    pThis->showScreenMessage(strMessage);
}

void GPSDevice::buttonEventCB(void* pCtx, ButtonEvent eType)
{
    GPSDevice* pThis = reinterpret_cast<GPSDevice*>(pCtx);
    if (nullptr == pThis)
    {
        LogInfo("buttonEventCB: pCtx is null");
        return;
    }

    // Handle button events here
    critical_section_enter_blocking(&pThis->m_CallbackCs);
    switch (eType)
    {
    case ButtonEvent::Tap:
        pThis->m_eLastButtonEvent = ButtonEvent::Tap;
        break;
    case ButtonEvent::Press:
        pThis->m_eLastButtonEvent = ButtonEvent::Press;
        break;
    case ButtonEvent::LongPress:
        pThis->m_eLastButtonEvent = ButtonEvent::LongPress;
        break;
    default:
        break;
    }
    critical_section_exit(&pThis->m_CallbackCs);
}


//
// Queue management methods
//
bool GPSDevice::enqueueGPSData(queue_t& q, const GPSData::Shared& spData)
{
    GPSData::Shared* pspData = new GPSData::Shared(spData);
    if (!queue_try_add(&q, &pspData))
    {
        delete pspData;
        return false;
    }
    return true;
}

GPSData::Shared GPSDevice::dequeueLatestGPSData(queue_t& q)
{
    GPSData::Shared spLatest;
    GPSData::Shared* pspData = nullptr;
    while (queue_try_remove(&q, &pspData))
    {
        if (pspData)
        {
            spLatest = *pspData; // shares ownership of the underlying GPSData with the queued copy
            delete pspData;      // only deletes the heap-allocated wrapper, not the GPSData itself
        }
        pspData = nullptr;
    }
    return spLatest;
}

void GPSDevice::drawSatGrid(const GPSData::Shared& spGPSData, uint xCenter, uint yCenter, uint radius, uint nRings)
{
    for (uint i = 1; i <= nRings; ++i)
    {
        m_spDisplay->Ellipse(xCenter, yCenter, radius * i / nRings, radius * i / nRings, m_foregroundColour);
    }

    m_spDisplay->VLine(xCenter, yCenter - radius - 2, 2 * radius + 5, m_foregroundColour);
    m_spDisplay->HLine(xCenter - radius - 2, yCenter, 2 * radius + 5, m_foregroundColour);
    m_spDisplay->Text("^", xCenter - getCharWidth() / 2 + 1, yCenter - radius - getCharHeight() / 2, m_northColour);

    int satRadius = m_satIconRadius / 2;
    if (!spGPSData->strLatitude.empty())
    {
        satRadius = m_satIconRadius;
    }
    for (auto oEntry : spGPSData->mSatList)
    {
        auto oSat = oEntry.second;
        double elrad = oSat.m_el * pi / 180;
        double azrad = oSat.m_az * pi / 180;
        drawCircleSat(xCenter, yCenter, radius, elrad, azrad, satRadius, COLOUR_WHITE, COLOUR_BLACK);
        for (auto nSat : spGPSData->vUsedList)
        {
            if (oSat.m_num == nSat)
            {
                drawCircleSat(xCenter, yCenter, radius, elrad, azrad, satRadius, COLOUR_WHITE, COLOUR_BLUE);
                break;
            }
        }
    }
}

void GPSDevice::drawCircleSat(uint gridCenterX,
                              uint gridCenterY,
                              uint nGridRadius,
                              float elrad,
                              float azrad,
                              uint satRadius,
                              uint16_t color,
                              uint16_t fillColor)
{
    // Draw satellite (fill first, then draw open circle)
    int dx = (nGridRadius - m_satIconRadius) * cos(elrad) * sin(azrad);
    int dy = (nGridRadius - m_satIconRadius) * cos(elrad) * -cos(azrad);
    int x = gridCenterX + dx;
    int y = gridCenterY + dy;
    Ellipse(x, y, satRadius, satRadius, fillColor, true); // Clear area with fill
    Ellipse(x, y, satRadius, satRadius, color);           // Draw circle without fill
}

void GPSDevice::drawClock(uint x, uint y, uint radius, std::string strTime)
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
    Ellipse(xCenter, yCenter, radius, radius, m_clockRingColour, false);
    Ellipse(xCenter, yCenter, radius - 1, radius - 1, m_clockFaceColour, true);
    // Draw quarter dots
    for (uint degDot = 0; degDot < 360; degDot += 30)
    {
        uint16_t colDot = (degDot % 90 == 0) ? m_clockQuarterDotsColour : m_clockDotsColour;
        uint16_t sizDot = (degDot % 90 == 0) ? 2 : 1;
        uint dxDot = int((radius - sizDot) * sin(degDot * pi / 180));
        uint dyDot = int((radius - sizDot) * -cos(degDot * pi / 180));
        Ellipse(xCenter + dxDot, yCenter + dyDot, sizDot, sizDot, colDot, true);
    }
    // Draw the hands
    Line(xCenter, yCenter, xCenter + dxs, yCenter + dys, m_clockSecondHandColour);
    if (m_bClockThickHands)
    {
        auto drawThickHand = [this, xCenter, yCenter](int dx, int dy) {
            int xTip = xCenter + dx;
            int yTip = yCenter + dy;
            int x = xCenter;
            int y = yCenter;
            int deltaX = abs(xTip - x);
            int stepX = (x < xTip) ? 1 : -1;
            int deltaY = -abs(yTip - y);
            int stepY = (y < yTip) ? 1 : -1;
            int error = deltaX + deltaY;

            while (true)
            {
                if (x == xTip && y == yTip)
                {
                    SetPixel(x, y, m_clockHandsColour);
                    break;
                }

                FillRect(x - 1, y - 1, 3, 3, m_clockHandsColour);

                int doubledError = 2 * error;
                if (doubledError >= deltaY)
                {
                    error += deltaY;
                    x += stepX;
                }
                if (doubledError <= deltaX)
                {
                    error += deltaX;
                    y += stepY;
                }
            }
        };
        drawThickHand(dxh, dyh);
        drawThickHand(dxm, dym);
    }
    else
    {
        Line(xCenter, yCenter, xCenter + dxh, yCenter + dyh, m_clockHandsColour);
        Line(xCenter, yCenter, xCenter + dxm, yCenter + dym, m_clockHandsColour);
    }
}

void GPSDevice::drawBarGraph(const GPSData::Shared& spGPSData, uint x, uint y, uint width, uint height)
{
    const BitmapFont* pFont = get_recommended_font(12); // 6x12, narrowest font for bar graph

    uint nMaxSats = 16;
    bool bNarrow = (nMaxSats * (2 * (pFont->width + 4)) > width);
    uint charWidth = bNarrow ? pFont->width : getCharWidth();
    uint charHeight = bNarrow ? pFont->height : getCharHeight();
    uint barDelta = bNarrow ? std::max(std::min(charWidth * 2 + 4, width / nMaxSats), charWidth * 2)
                            : std::max(std::min(charWidth + 4, width / nMaxSats), charWidth);
    uint barWidth = barDelta - 2;
    uint barPosX = x + width - (spGPSData->mSatList.size() * barDelta);
    uint barHeightMax = height - (charHeight + 1) * 2;

    for (auto oEntry : spGPSData->mSatList)
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
        for (auto nSat : spGPSData->vUsedList)
        {
            if (oSat.m_num == nSat)
            {
                if (barHeight > 0)
                {
                    // draw inner filled rectangle to indicate used satellite
                    m_spDisplay->Rect(barPosX + 2, baseLineY - barHeight + 2, barWidth - 2, barHeight - 2, m_barsColour, true);
                }
                break;
            }
        }
        barPosX += barDelta;
    }
}

int GPSDevice::linePos(int nLine)
{
    if (nLine >= 0)
    {
        return nLine * getLineAdvance();
    }
    else
    {
        // Anchor the last line (-1) fully on screen so descenders aren't clipped,
        // then step upward by the line advance for -2, -3, ...
        return Height() - getCharHeight() + ((nLine + 1) * getLineAdvance());
    }
}

void GPSDevice::drawText(int nLine, std::string strText, uint16_t color, bool bRightAlign, uint nPadding)
{
    int x = (!bRightAlign) ? 0 : Width() - (strText.length() * getCharWidth());
    int y = linePos(nLine);
    x = bRightAlign ? x - nPadding : x + nPadding;
    Text(strText.c_str(), x, y, color);
}

void GPSDevice::drawTextCentered(int nLine, std::string strText, uint16_t color)
{
    int x = (Width() - (strText.length() * getCharWidth())) / 2;
    int y = linePos(nLine);
    Text(strText.c_str(), x, y, color);
}
