/*
 * GPS display base class. Commonality for gathering and representing information
 * from a GPS receiver.
 *
 * (c) 2023-2026 Erik Tkal
 *
 */

#pragma once

#include <memory>
#include <string>

#include "pico/util/queue.h"

#include "gps.h"
#include "display.h"
#include "button.h"
#include "font.h"

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


class GPSDevice : public std::enable_shared_from_this<GPSDevice>
{
public:
    typedef std::shared_ptr<GPSDevice> Shared;

    GPSDevice(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton);
    virtual ~GPSDevice() = default;

    virtual void Initialize();
    virtual void Start();
    virtual void Run();
    virtual void DoWork();
    virtual void Stop();
    virtual bool GetStatus(GPS_Status& status);

protected:
    ButtonEvent getLastButtonEvent();
    virtual void showSplashScreen() {};

private:
    explicit GPSDevice() = default;

    static void gpsDataCB(void* pCtx, GPSData::Shared spGPSData);
    static void messageCB(void* pCtx, std::string strMessage);
    static void buttonEventCB(void* pCtx, ButtonEvent eType);

    // Hand a GPSData::Shared across a pico queue_t via a heap-allocated shared_ptr wrapper,
    // so the underlying object's lifetime is managed safely (and only) via reference counting.
    static bool enqueueGPSData(queue_t& q, const GPSData::Shared& spData);
    // Drain a queue of heap-allocated shared_ptr wrappers, keeping only the most recent GPSData.
    static GPSData::Shared dequeueLatestGPSData(queue_t& q);

protected:
    virtual bool handleButtonEvent() = 0;
    virtual void showScreenMessage(std::string strMessage) = 0;
    virtual void updateUI(GPSData::Shared spGPSData) = 0;

    void drawSatGrid(const GPSData::Shared& spGPSData, uint xCenter, uint yCenter, uint radius, uint nRings = 3);
    void drawClock(uint x, uint y, uint radius, std::string strTime);
    void drawCircleSat(uint gridCenterX,
                       uint gridCenterY,
                       uint nGridRadius,
                       float elrad,
                       float azrad,
                       uint satRadius,
                       uint16_t color = COLOUR_WHITE,
                       uint16_t fillColor = COLOUR_WHITE);
    void drawBarGraph(const GPSData::Shared& spGPSData, uint x, uint y, uint width, uint height);
    int linePos(int nLine);
    void drawText(int nLine, std::string strText, uint16_t color = COLOUR_WHITE, bool bRightAlign = true, uint nPadding = 0);
    void drawTextCentered(int nLine, std::string strText, uint16_t color = COLOUR_WHITE);

    // Screen management - delegates to m_spDisplay
    void SetFont(const BitmapFont* pFont)
    {
        m_spDisplay->SetFont(pFont);
    }
    const BitmapFont* GetFont() const
    {
        return m_spDisplay->GetFont();
    }
    void Clear()
    {
        m_spDisplay->Clear();
    }
    void Show()
    {
        m_spDisplay->Show();
    }
    uint16_t Width() const
    {
        return m_spDisplay->Width();
    }
    uint16_t Height() const
    {
        return m_spDisplay->Height();
    }
    void SetPixel(uint x, uint y, uint16_t color)
    {
        m_spDisplay->SetPixel(x, y, color);
    }
    void Ellipse(uint xCenter, uint yCenter, uint xRadius, uint yRadius, uint16_t color, bool bFill = false)
    {
        m_spDisplay->Ellipse(xCenter, yCenter, xRadius, yRadius, color, bFill);
    }
    void Line(uint x1, uint y1, uint x2, uint y2, uint16_t color)
    {
        m_spDisplay->Line(x1, y1, x2, y2, color);
    }
    void Text(const char* str, int x, int y, uint16_t color)
    {
        m_spDisplay->Text(str, x, y, color);
    }
    void Text(const char* str, int x, int y, uint16_t color, int scale)
    {
        m_spDisplay->Text(str, x, y, color, scale);
    }
    void Text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1)
    {
        m_spDisplay->Text(str, x, y, color, font, scale);
    }
    void Fill(uint16_t color)
    {
        m_spDisplay->Fill(color);
    }
    void VLine(uint x, uint y, uint height, uint16_t color)
    {
        m_spDisplay->VLine(x, y, height, color);
    }
    void HLine(uint x, uint y, uint width, uint16_t color)
    {
        m_spDisplay->HLine(x, y, width, color);
    }
    void Rect(uint x, uint y, uint width, uint height, uint16_t color, bool bFill = false)
    {
        m_spDisplay->Rect(x, y, width, height, color, bFill);
    }
    void FillRect(uint x, uint y, uint width, uint height, uint16_t color)
    {
        m_spDisplay->FillRect(x, y, width, height, color);
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

protected:
    Display::Shared m_spDisplay;

private:
    bool m_bExit {false};
    bool m_bInitialized {false};
    GPS::Shared m_spGPS;
    Button::Shared m_spButton;
    queue_t m_qIncomingGPSData;       // Queue of GPS data from the source
    GPSData::Shared m_spLastGPSData;  // Last GPS data seen, for immediate button-triggered redraws
    AlarmTimer::Shared m_spIdleTimer; // Timer to detect lack of GPS data
    bool m_bShowWaitingForGPS {false};

    // Critical section for protecting callback-related member variables below
    critical_section m_CallbackCs;
    bool m_bHasPosition {false};
    bool m_bExternalAntenna {false};
    std::string m_strGpsTimeRaw;
    std::string m_strGpsDateRaw;
    bool m_bStatusChanged {false};
    ButtonEvent m_eLastButtonEvent {ButtonEvent::None};

protected:
    // Various colors and such that apply to different displays
    uint16_t m_satIconRadius {2};
    uint16_t m_northColour {COLOUR_RED};
    uint16_t m_backgroundColour {COLOUR_BLACK};
    uint16_t m_foregroundColour {COLOUR_WHITE};

    uint16_t m_clockHandsColour {COLOUR_WHITE};
    uint16_t m_clockSecondHandColour {COLOUR_RED};
    uint16_t m_clockDotsColour {COLOUR_GRAY};
    uint16_t m_clockQuarterDotsColour {COLOUR_BLUE};
    uint16_t m_clockFaceColour {COLOUR_BLACK};
    uint16_t m_clockRingColour {COLOUR_LIME};
    bool m_bClockThickHands {false};

    uint16_t m_barsColour {COLOUR_BLUE};
};