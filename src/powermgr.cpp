/*
 * PowerMgr class for getting voltage reading from ADC.
 *
 * Copyright (c) 2026 Erik Tkal
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
 * 
 * Original implementation based on Raspberry Pi Pico SDK examples for ADC voltage
 * reading, falls under the following copyright:
 * 
**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "powermgr.h"

#include "hardware/adc.h"

#include "log.h"

#if CYW43_USES_VSYS_PIN
#include "pico/cyw43_arch.h"
#endif

#ifndef PICO_POWER_SAMPLE_COUNT
#define PICO_POWER_SAMPLE_COUNT 3
#endif
#ifndef ADC_REF_ADJUST
#define ADC_REF_ADJUST 1.0f
#endif
#ifndef SCHOTTKY_ADJUST
#define SCHOTTKY_ADJUST 0.0f
#endif

// Pin used for ADC 0
#define PICO_FIRST_ADC_PIN 26

PowerMgr::Shared PowerMgr::sm_spPowerMgr;

PowerMgr::PowerMgr(uint8_t gpioPin)
    : m_gpioPin(gpioPin)
{
    critical_section_init(&m_Cs);
    adc_init();
}


PowerMgr::Shared PowerMgr::GetInstance()
{
    return sm_spPowerMgr;
}

void PowerMgr::InitializeSingleton(uint8_t gpioPin)
{
    if (!sm_spPowerMgr)
    {
        sm_spPowerMgr = Shared(new PowerMgr(gpioPin));
        return;
    }
}

float PowerMgr::GetVoltage(bool bForceSample)
{
    float fVoltage = 0.0;
    bool bUsingBattery = false;
    if (bForceSample)
    {
        int iRet = sample(&fVoltage, &bUsingBattery);
        if (iRet != PICO_OK)
        {
            return 0.0;
        }
        critical_section_enter_blocking(&m_Cs);
        m_fVoltage = fVoltage;
        m_uVoltage_mV = static_cast<uint32_t>(fVoltage * 1000.0f + 0.5f);
        m_bUsingBattery = bUsingBattery;
        critical_section_exit(&m_Cs);
    }
    critical_section_enter_blocking(&m_Cs);
    fVoltage = m_fVoltage;
    critical_section_exit(&m_Cs);
    return fVoltage;
}

// Integer millivolts, computed at sample time. Lets the other core format the value without float/double math.
uint32_t PowerMgr::GetVoltage_mV()
{
    critical_section_enter_blocking(&m_Cs);
    uint32_t uVoltage_mV = m_uVoltage_mV;
    critical_section_exit(&m_Cs);
    return uVoltage_mV;
}

bool PowerMgr::UsingBattery()
{
    critical_section_enter_blocking(&m_Cs);
    bool bUsingBattery = m_bUsingBattery;
    critical_section_exit(&m_Cs);
    return bUsingBattery;
}

int PowerMgr::sample(float* pfVoltage, bool* pbUsingBattery)
{
    if (nullptr == pfVoltage || nullptr == pbUsingBattery)
    {
        LogInfo("Invalid arguments passed to sample");
        return PICO_ERROR_INVALID_ARG;
    }

#if !defined(PICO_VSYS_PIN) && !defined(ADC_GPIO_PIN)
    return PICO_ERROR_NO_DATA;
#endif

    if (m_gpioPin - PICO_FIRST_ADC_PIN < 0 || m_gpioPin - PICO_FIRST_ADC_PIN > 3)
    {
        LogInfo("Invalid GPIO pin for ADC");
        return PICO_ERROR_INVALID_ARG;
    }

#if CYW43_USES_VSYS_PIN
    cyw43_thread_enter();
    // Make sure cyw43 is awake
    cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
#endif

    // Sample the ADC to determine the battery voltage

    // setup adc
    adc_gpio_init(m_gpioPin);
    adc_select_input(m_gpioPin - PICO_FIRST_ADC_PIN);

    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    // We seem to read low values initially - this seems to fix it
    int ignore_count = PICO_POWER_SAMPLE_COUNT;
    while (!adc_fifo_is_empty() || ignore_count-- > 0)
    {
        (void)adc_fifo_get_blocking();
    }

    // read voltage
    uint32_t voltage = 0;
    for (int i = 0; i < PICO_POWER_SAMPLE_COUNT; i++)
    {
        uint16_t val = adc_fifo_get_blocking();
        voltage += val;
    }

    adc_run(false);
    adc_fifo_drain();

    voltage /= PICO_POWER_SAMPLE_COUNT;
#if CYW43_USES_VSYS_PIN
    cyw43_thread_exit();
#endif
    // Generate voltage
    const float conversion_factor = 3.3f / (1 << 12);
    *pfVoltage = voltage * 3 * conversion_factor * ADC_REF_ADJUST;

    // Check if we are using battery
#if defined CYW43_WL_GPIO_VBUS_PIN
    *pbUsingBattery = !cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
#elif defined(PICO_VBUS_PIN)
    gpio_set_function(PICO_VBUS_PIN, GPIO_FUNC_SIO);
    *pbUsingBattery = !gpio_get(PICO_VBUS_PIN);
#else
    *pbUsingBattery = (*pfVoltage < (4.3f - SCHOTTKY_ADJUST)) ? true : false;
#endif

    if (*pbUsingBattery)
    {
        *pfVoltage += SCHOTTKY_ADJUST;
    }

    return PICO_OK;
}
