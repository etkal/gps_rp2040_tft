/*
 * GPS using TFT display
 *
 * (c) 2024-2026 Erik Tkal
 *
 */

#include <stdio.h>
#include <queue>
#include <memory>

#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#include "timemgr.h"
#include "gps.h"
#include "ili_tft.h"
#include "led.h"
#include "font.h"

// GPS_TFT class
//
// This combines an TFT display, GPS module and LED.
// The devices are initialized here and then callbacks are set up in order
// to receive data from the GPS, and the resulting data is displayed
// and the LED can be used to indicate a position lock and/or other
// status.
//
class GPS_TFT
{
public:
    typedef std::shared_ptr<GPS_TFT> Shared;

    GPS_TFT(ILI_TFT::Shared spDisplay, GPS::Shared spGPS, LED::Shared spLED);
    ~GPS_TFT();

    void Initialize();
    void Run();

private:
    static void gpsDataCB(void* pCtx, GPSData::Shared spGPSData);

    void showWaitingForGPS();
    void updateUI(GPSData::Shared spGPSData);
    void drawSatGrid(uint xCenter, uint yCenter, uint radius, uint nRings = 3);
    void drawBarGraph(uint x, uint y, uint width, uint height);
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

    ILI_TFT::Shared m_spDisplay;
    GPS::Shared m_spGPS;
    LED::Shared m_spLED;
    GPSData::Shared m_spGPSData;            // Current data being used for display
    std::queue<GPSData::Shared> m_qGPSData; // Queue of GPS data to be processed by the display loop
    uint64_t m_nLastTimeSyncAttemptSec;
    critical_section m_GpsDataCallbackCS; // Protects access to GPS data queue
    AlarmTimer::Shared m_spIdleTimer; // Timer to detect lack of GPS data
};
