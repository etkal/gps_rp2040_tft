/*
 * Pico LED class
 *
 * Copyright (c) 2025-2026 Erik Tkal
 *
 */

#pragma once

#include <vector>
#include <deque>
#include <memory>

#include "pico/stdlib.h"

#if PICO_DEFAULT_LED_PIN_INVERTED
auto constexpr LED_ON = 0;
auto constexpr LED_OFF = 1;
#else
auto constexpr LED_ON = 1;
auto constexpr LED_OFF = 0;
#endif

#if defined(PICO_DEFAULT_WS2812_POWER_PIN)
#define WS2812_POWER_PIN PICO_DEFAULT_WS2812_POWER_PIN
#else
#define WS2812_POWER_PIN 0
#endif

auto constexpr max_lum = 20;

static inline constexpr uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
#if defined(WS2812_LED_IS_RGB)
    return ((uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)(b));
#else
    return ((uint32_t)(g << 16) | (uint32_t)(r << 8) | (uint32_t)(b));
#endif
}

static inline uint32_t scale_color(uint32_t color, uint8_t factor = max_lum)
{
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t r = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    return (((uint32_t)(g * factor / 256) << 16) | (uint32_t)(r * factor / 256) << 8) | (uint32_t)(b * factor / 256);
}

auto constexpr led_white = urgb_u32(0x80, 0x80, 0xFF);
auto constexpr led_on = urgb_u32(0x80, 0x80, 0xFF);
auto constexpr led_black = urgb_u32(0, 0, 0);
auto constexpr led_off = urgb_u32(0, 0, 0);

auto constexpr led_red = urgb_u32(0x80, 0, 0);
auto constexpr led_green = urgb_u32(0, 0x80, 0);
auto constexpr led_blue = urgb_u32(0, 0, 0xFF);
auto constexpr led_cyan = urgb_u32(0, 0x80, 0xFF);
auto constexpr led_magenta = urgb_u32(0x80, 0, 0xFF);
auto constexpr led_yellow = urgb_u32(0x80, 0x80, 0);
auto constexpr led_orange = urgb_u32(0x90, 0x40, 0);

constexpr uint led_all = UINT32_MAX;

class LED : public std::enable_shared_from_this<LED>
{
public:
    typedef std::shared_ptr<LED> Shared;

    LED();
    virtual ~LED();

    virtual void Initialize() = 0;
    virtual void On(uint idx = 0) = 0;
    virtual void Off(uint idx = 0) = 0;
    virtual void Show() = 0;
    virtual void SetPixel(uint idx, uint32_t color, uint8_t brightness = 0) = 0;
    virtual uint32_t GetPixel(uint idx) = 0;
    virtual void SetIgnore(std::vector<uint32_t> vIgnore) {};
    void Blink_ms(uint idx = 0, uint duration = 50);
    void DoWork();
    static LED::Shared GetLED(uint nLEDIndex = 0);

protected:
    class BlinkContext
    {
    public:
        uint64_t offTime {};
    };

    std::vector<BlinkContext> m_BlinkContexts;
    explicit LED(uint numPixels);

private:
    static std::deque<LED::Shared> sm_mapLEDs;
    size_t m_nIndexInMapLEDs {0};
};

class LED_pico : public LED
{
public:
    LED_pico(uint pin);
    LED_pico();
    virtual ~LED_pico();

    void Initialize() override;
    void On(uint idx = 0) override;
    void Off(uint idx = 0) override;
    void Show() override;
    void SetPixel(uint idx, uint32_t color, uint8_t brightness = 0) override;
    uint32_t GetPixel(uint idx) override;
    void SetIgnore(std::vector<uint32_t> vIgnore) override;

protected:
    uint m_nPin;
    uint32_t m_nColor;
    std::vector<uint32_t> m_vIgnore;
};

#if defined(PLATFORM_PICO_W)
class LED_pico_w : public LED_pico
{
public:
    LED_pico_w(uint pin);
    virtual ~LED_pico_w();

    void Initialize() override;
    void Show() override;
};
#endif

class LED_neo : public LED
{
public:
    LED_neo(uint numLEDs, uint pin, uint powerPin = WS2812_POWER_PIN, bool bIsRGBW = false);
    virtual ~LED_neo();

    void Initialize() override;
    void On(uint idx = 0) override;
    void Off(uint idx = 0) override;
    void Show() override;
    void SetPixel(uint idx, uint32_t color, uint8_t brightness = 0) override;
    uint32_t GetPixel(uint idx) override;

private:
    uint m_nPin;
    uint m_nPowerPin;
    uint m_nNumLEDs;
    bool m_bIsRGBW;
    std::vector<uint32_t> m_vPixels;
};
