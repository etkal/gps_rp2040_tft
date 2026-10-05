/*
 * Pico LED class
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

#include "led.h"

#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif
#include "ws2812.pio.h"

std::deque<LED::Shared> LED::sm_mapLEDs;

LED::LED()
    : LED(1)
{
}

LED::LED(uint numPixels)
    : m_BlinkContexts(numPixels),
      m_nIndexInMapLEDs(sm_mapLEDs.size())
{
    sm_mapLEDs.push_back(Shared(this));
}

LED::~LED()
{
    sm_mapLEDs.erase(sm_mapLEDs.begin() + m_nIndexInMapLEDs);
}

void LED::Blink_ms(uint idx, uint duration)
{
    const uint64_t offTime = time_us_64() + static_cast<uint64_t>(duration) * 1000;
    const uint64_t deadline = offTime == 0 ? 1 : offTime;
    if (idx == led_all)
    {
        for (BlinkContext& context : m_BlinkContexts)
        {
            context.offTime = deadline;
        }
    }
    else if (idx < m_BlinkContexts.size())
    {
        m_BlinkContexts[idx].offTime = deadline;
    }

    On(idx);
}

void LED::DoWork()
{
    const uint64_t now = time_us_64();
    bool bShow = false;
    for (size_t idx = 0; idx < m_BlinkContexts.size(); ++idx)
    {
        BlinkContext& context = m_BlinkContexts[idx];
        if (context.offTime != 0 && now >= context.offTime)
        {
            SetPixel(static_cast<uint>(idx), led_off);
            context.offTime = 0;
            bShow = true;
        }
    }

    if (bShow)
    {
        Show();
    }
}

LED::Shared LED::GetLED(uint nLEDIndex)
{
    if (nLEDIndex < sm_mapLEDs.size())
    {
        return sm_mapLEDs[nLEDIndex];
    }
    return nullptr;
}

//
// LED_pico - Raspberry Pi Pico GPIO LED support
//

LED_pico::LED_pico(uint pin)
    : m_nPin(pin),
      m_nColor(led_white)
{
}

LED_pico::LED_pico()
    : m_nPin(-1),
      m_nColor(led_white)
{
    // Default constructor for LED_pico, pin will need to be set later, used by LED_pico_w to avoid
    // calling gpio_init() in the base class constructor.
}

LED_pico::~LED_pico()
{
    Off();
    gpio_deinit(m_nPin);
}

void LED_pico::Initialize()
{
    gpio_init(m_nPin);
    gpio_set_dir(m_nPin, GPIO_OUT);
    Off();
}

void LED_pico::On(uint idx)
{
    if (idx == 0 || idx == led_all)
    {
        Show();
    }
}

void LED_pico::Off(uint idx)
{
    if (idx == 0 || idx == led_all)
    {
        m_nColor = led_off;
        Show();
    }
}

void LED_pico::Show()
{
    if (m_nColor != led_off)
    {
        gpio_put(m_nPin, LED_ON);
    }
    else
    {
        gpio_put(m_nPin, LED_OFF);
    }
}

void LED_pico::SetPixel(uint idx, uint32_t color, uint8_t brightness)
{
    if (idx == 0 || idx == led_all)
    {
        for (auto i : m_vIgnore)
        {
            if (i == color)
            {
                color = led_off;
                return;
            }
        }
        m_nColor = color;
    }
}

uint32_t LED_pico::GetPixel(uint idx)
{
    return m_nColor;
}

void LED_pico::SetIgnore(std::vector<uint32_t> vIgnore)
{
    m_vIgnore = vIgnore;
}

//
// LED_pico_w - Raspberry Pi Pico W GPIO LED support
//

#if defined(PLATFORM_PICO_W)
LED_pico_w::LED_pico_w(uint pin)
{
    m_nPin = pin;
}

LED_pico_w::~LED_pico_w()
{
    Off();
}

void LED_pico_w::Initialize()
{
    Off();
}

void LED_pico_w::Show()
{
    cyw43_thread_enter();
    if (m_nColor != led_off)
    {
        cyw43_arch_gpio_put(m_nPin, 1);
    }
    else
    {
        cyw43_arch_gpio_put(m_nPin, 0);
    }
    cyw43_thread_exit();
}
#endif

//
// LED_neo - WS2812 LED support
//

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

LED_neo::LED_neo(uint numLEDs, uint pin, uint powerPin, bool bIsRGBW)
    : LED(numLEDs),
      m_nPin(pin),
      m_nPowerPin(powerPin),
      m_nNumLEDs(numLEDs),
      m_bIsRGBW(bIsRGBW)
{
}

LED_neo::~LED_neo()
{
    Off();
    if (0 != m_nPowerPin)
    {
        gpio_put(m_nPowerPin, 0);
        gpio_deinit(m_nPowerPin);
    }
}

void LED_neo::Initialize()
{
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, m_nPin, 800000, m_bIsRGBW);

    if (0 != m_nPowerPin)
    {
        gpio_init(m_nPowerPin);
        gpio_set_dir(m_nPowerPin, GPIO_OUT);
        gpio_put(m_nPowerPin, 1);
    }

    m_vPixels.resize(m_nNumLEDs);
    Off(led_all);
    sleep_us(300);
}

void LED_neo::On(uint idx)
{
    Show();
}

void LED_neo::Off(uint idx)
{
    SetPixel(idx, led_off);
    Show();
}

void LED_neo::Show()
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        put_pixel(m_vPixels[i]);
    }
}

void LED_neo::SetPixel(uint idx, uint32_t color, uint8_t brightness)
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        if (i == idx || led_all == idx)
        {
            m_vPixels[i] = scale_color(color, 0 == brightness ? max_lum : brightness);
        }
    }
}

uint32_t LED_neo::GetPixel(uint idx)
{
    if (idx < m_nNumLEDs)
    {
        return m_vPixels[idx];
    }
    return led_off;
}
