/*
 * PowerMgr class for getting voltage reading from ADC
 *
 * Copyright (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "pico/critical_section.h"

class PowerMgr
{
public:
    typedef std::shared_ptr<PowerMgr> Shared;

    ~PowerMgr() = default;

    static Shared GetInstance();
    static void InitializeSingleton(uint8_t gpioPin);

    // ADC voltage in volts
    float GetVoltage(bool bForceSample = false);
    // ADC voltage in millivolts
    uint32_t GetVoltage_mV();
    // Using battery or not (might be a guess)
    bool UsingBattery();

private:
    explicit PowerMgr(uint8_t gpioPin);

    int sample(float* pfVoltage, bool* pbUsingBattery);

    uint8_t m_gpioPin;
    volatile float m_fVoltage {0.0f};
    volatile uint32_t m_uVoltage_mV {0};
    volatile bool m_bUsingBattery {false};
    static Shared sm_spPowerMgr;
    critical_section m_Cs;
};
