/*
 * Pico LED class
 *
 * (c) 2025-2026 Erik Tkal
 *
 */

#include "pico/stdlib.h"
#if defined(PLATFORM_PICO_W)
#include "pico/cyw43_arch.h"
#endif
#include "led.h"
#include "ws2812.pio.h"

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

// Use repeating_timer to avoid hangs in sleep_ms with pico_w
static bool ledTimerCallback(repeating_timer_t* pTimer)
{
    LED* pThis = reinterpret_cast<LED*>(pTimer->user_data);
    pThis->Off();
    return false; // cancels
}

LED::~LED()
{
    cancel_repeating_timer(&m_LedTimer);
}

void LED::Blink_ms(uint duration, uint32_t color)
{
    On();
    add_repeating_timer_ms(duration, ledTimerCallback, reinterpret_cast<void*>(this), &m_LedTimer);
}


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

void LED_pico::On()
{
    for (auto i : m_vIgnore)
    {
        if (i == m_nColor)
        {
            return;
        }
    }
    gpio_put(m_nPin, LED_ON);
}

void LED_pico::Off()
{
    gpio_put(m_nPin, LED_OFF);
}

void LED_pico::SetPixel(uint idx, uint32_t color)
{
    m_nColor = color;
}

void LED_pico::SetIgnore(std::vector<uint32_t> vIgnore)
{
    m_vIgnore = vIgnore;
}


LED_neo::LED_neo(uint numLEDs, uint pin, uint powerPin, bool bIsRGBW)
    : m_nPin(pin),
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
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, m_nPin, 800000, m_bIsRGBW);

    if (0 != m_nPowerPin)
    {
        gpio_init(m_nPowerPin);
        gpio_set_dir(m_nPowerPin, GPIO_OUT);
        gpio_put(m_nPowerPin, 1);
    }

    m_vPixels.resize(m_nNumLEDs);
    Off();
}

void LED_neo::On()
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        put_pixel(m_vPixels[i]);
    }
}

void LED_neo::Off()
{
    for (size_t i = 0; i < m_nNumLEDs; ++i)
    {
        put_pixel(0);
    }
}

void LED_neo::SetPixel(uint idx, uint32_t color)
{
    m_vPixels[idx] = color;
}

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

void LED_pico_w::On()
{
    for (auto i : m_vIgnore)
    {
        if (i == m_nColor)
        {
            return;
        }
    }
    cyw43_arch_gpio_put(m_nPin, 1);
}

void LED_pico_w::Off()
{
    cyw43_arch_gpio_put(m_nPin, 0);
}
#endif
