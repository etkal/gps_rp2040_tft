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

    GPS_TFT(Display::Shared spDisplay, GPS::Shared spGPS, Button::Shared spButton);
    ~GPS_TFT();

private:
    bool handleButtonEvent() override;
    void showScreenMessage(std::string strMessage) override;

    void updateUI(GPSData::Shared spGPSData);

    void showSplashScreen() override;

    DisplayMode m_eDisplayMode {DisplayMode::ModeFull};
    SpeedUnit m_eSpeedUnit {SpeedUnit::Mph};
    bool m_bTextTime {false};
    bool m_bAltitudeFeet {true};
};
