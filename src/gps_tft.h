/*
 * GPS using TFT display
 *
 * (c) 2024-2026 Erik Tkal
 *
 */

#pragma once

#include <stdio.h>
#include <memory>

#include "display.h"
#include "gpsdevice.h"

enum class DisplayMode
{
    ModeFull,
    ModeTime,
    ModePosition,
    ModeSpeed
};

// Touch area IDs. A namespace-scoped unscoped enum keeps the TouchArea::Name syntax while converting
// implicitly to uint for use as a Touch area ID.
namespace TouchArea
{
    enum Id : uint
    {
        Clock,
        SatGrid,
        TextArea,
        BarGraph,
        UpperLeft,
        LowerLeft
    };

    static inline std::string GetName(Id id)
    {
        switch (id)
        {
        case Clock:
            return "Clock";
        case SatGrid:
            return "SatGrid";
        case TextArea:
            return "TextArea";
        case BarGraph:
            return "BarGraph";
        case UpperLeft:
            return "UpperLeft";
        case LowerLeft:
            return "LowerLeft";
        default:
            return "Unknown";
        }
    }
} // namespace TouchArea

// GPS_TFT class
//
// This represents a TFT display showing information from a GPS module.
// The device is initialized here and then callbacks are set up in order
// to receive data from the GPS, and the resulting data is displayed.
//
class GPS_TFT : public GPSDevice
{
public:
    typedef std::shared_ptr<GPS_TFT> Shared;

    GPS_TFT(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton, Touch::Shared spTouch = nullptr);
    ~GPS_TFT();

    void Initialize() override;

private:
    bool handleButtonEvent() override;
    bool handleTouchEvent(const TouchEvent& event) override;
    void addTouchAreasIfNeeded();
    void showScreenMessage(std::string strMessage) override;

    void updateUI(GPSData::Shared spGPSData);

    void showSplashScreen() override;

    bool m_bTouchAreasAdded {false};
    DisplayMode m_eDisplayMode {DisplayMode::ModeFull};
    SpeedUnit m_eSpeedUnit {SpeedUnit::Mph};
    bool m_bTextTime {false};
    bool m_bAltitudeFeet {true};

    // UI widget placement information (for drawing and touch recognition)
    const BitmapFont* m_pBaseFont {nullptr};
    uint m_nClockX {0};
    uint m_nClockY {0};
    uint m_nClockRadius {0};
    uint m_nSatGridX {0};
    uint m_nSatGridY {0};
    uint m_nSatGridRadius {0};
    uint m_nTextAreaX {0};
    uint m_nTextAreaY {0};
    uint m_nTextAreaWidth {0};
    uint m_nTextAreaHeight {0};
    uint m_nBarGraphX {0};
    uint m_nBarGraphY {0};
    uint m_nBarGraphWidth {0};
    uint m_nBarGraphHeight {0};
    uint m_nUpperLeftX {0};
    uint m_nUpperLeftY {0};
    uint m_nUpperLeftRadius {0};
    uint m_nLowerLeftX {0};
    uint m_nLowerLeftY {0};
    uint m_nLowerLeftRadius {0};

    uint m_nTappedAreaId {0};
    bool m_bDisplayTapEcho {false};
};
