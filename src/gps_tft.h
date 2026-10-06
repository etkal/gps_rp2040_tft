/*
 * GPS using TFT display
 *
 * (c) 2024-2026 Erik Tkal
 *
 */

#pragma once

#include <stdio.h>
#include <memory>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#include "ili_tft.h"
#include "gps.h"
#include "led.h"
#include "button.h"
#include "font.h"
#include "timers.h"

enum class SpeedUnit
{
    Mph,
    Kph,
    Knots
};

class GPS_Status
{
public:
    bool bHasPosition {false};
    bool bExternalAntenna {false};
    std::string strGpsTimeRaw;
    std::string strGpsDateRaw;
};

// GPS_TFT class
//
// This represents a TFT display showing information from a GPS module.
// The device is initialized here and then callbacks are set up in order
// to receive data from the GPS, and the resulting data is displayed.
//
class GPS_TFT : public std::enable_shared_from_this<GPS_TFT>
{
public:
    typedef std::shared_ptr<GPS_TFT> Shared;

    GPS_TFT(ILI_TFT::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton);
    ~GPS_TFT();

    void Initialize();
    void Start();
    void Run();
    void DoWork();
    void Stop();
    bool GetStatus(GPS_Status& status);

private:
    static void gpsDataCB(void* pCtx, GPSData::Shared spGPSData);
    static void messageCB(void* pCtx, std::string strMessage);
    static void buttonEventCB(void* pCtx, ButtonEvent eType);

    // Hand a GPSData::Shared across a pico queue_t via a heap-allocated shared_ptr wrapper,
    // so the underlying object's lifetime is managed safely (and only) via reference counting.
    static bool enqueueGPSData(queue_t& q, const GPSData::Shared& spData);
    // Drain a queue of heap-allocated shared_ptr wrappers, keeping only the most recent GPSData.
    static GPSData::Shared dequeueLatestGPSData(queue_t& q);

    bool handleButtonEvent();
    void showScreenMessage(std::string strMessage);
    void updateUI(GPSData::Shared spGPSData);
    void drawSatGrid(const GPSData::Shared& spGPSData, uint xCenter, uint yCenter, uint radius, uint nRings = 3);
    void drawBarGraph(const GPSData::Shared& spGPSData, uint x, uint y, uint width, uint height);
    void drawClock(uint x, uint y, uint radius, std::string strTime);
    void drawCircleSat(uint gridCenterX,
                       uint gridCenterY,
                       uint nGridRadius,
                       float elrad,
                       float azrad,
                       uint satRadius,
                       uint16_t color = COLOUR_WHITE,
                       uint16_t fillColor = COLOUR_WHITE);
    int linePos(int nLine);
    void drawText(int nLine, std::string strText, uint16_t color = COLOUR_WHITE, bool bRightAlign = true, uint nPadding = 0);
    void drawTextCentered(int nLine, std::string strText, uint16_t color = COLOUR_WHITE);
#if !defined(NDEBUG)
    void showSplashScreen();
#endif

    // Font management - delegates to m_spDisplay
    void SetFont(const BitmapFont* pFont)
    {
        if (m_spDisplay)
            m_spDisplay->SetFont(pFont);
    }
    const BitmapFont* GetFont() const
    {
        return m_spDisplay ? m_spDisplay->GetFont() : nullptr;
    }

    // Get current font dimensions dynamically from display's font
    inline uint getCharWidth() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->width : 8;
    }
    inline uint getCharHeight() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->height : 8;
    }
    inline uint getLineAdvance() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->effectiveLineAdvance() : 8;
    }

    bool m_bExit {false};
    bool m_bInitialized {false};
    ILI_TFT::Shared m_spDisplay;
    GPS::Shared m_spGPS;
    Button::Shared m_spButton;
    queue_t m_qIncomingGPSData; // Queue of GPS data from the source
    GPSData::Shared m_spLastGPSData;  // Last GPS data seen, for immediate button-triggered redraws
    AlarmTimer::Shared m_spIdleTimer;     // Timer to detect lack of GPS data
    bool m_bShowWaitingForGPS {false};
    bool m_bHasPosition {false};
    bool m_bExternalAntenna {false};
    std::string m_strGpsTimeRaw;
    std::string m_strGpsDateRaw;
    bool m_bStatusChanged {false};

    ButtonEvent m_eLastButtonEvent {ButtonEvent::None};
    SpeedUnit m_eSpeedUnit {SpeedUnit::Mph};
    bool m_bTextTime {false};
    bool m_bAltitudeFeet {true};
    critical_section m_CallbackCs;
};
