/*
 * Pico Button class
 *
 * Copyright (c) 2026 Erik Tkal
 *
 * Button class for handling GPIO button events on the Raspberry Pi Pico.
 *
 * Constructed with a GPIO pin number, debounce time, press time, and long press time.
 * Anything longer than the debounce timer is considered a valid press, with three levels
 * supported: tap, press and long press.
 *
 */

#pragma once

#include <memory>
#include <map>

#include "pico/stdlib.h"

#include "timers.h"

enum class ButtonEvent
{
    None,
    Tap,
    Press,
    LongPress
};


typedef void (*eventCallback)(void* pCtx, ButtonEvent eType);

class Button : public std::enable_shared_from_this<Button>
{
public:
    typedef std::shared_ptr<Button> Shared;

    Button(uint nButtonPin, uint nDebounceMs = 25, uint nPressMs = 500, uint nLongPressMs = 1500);
    virtual ~Button() = default;

    void Initialize();
    void SetEventCallback(void* pCtx, eventCallback pCB);
    static Button::Shared GetButton(uint nButtonPin);

private:
    static void irqHandler(uint gpio, uint32_t events);
    void onDebounceTimer();
    void onLongPressTimer();

    uint m_nPin {0};
    uint m_nDebounceMs {0};
    uint m_nPressMs {0};
    uint m_nLongPressMs {0};

    AlarmTimer m_debounceTimer;
    AlarmTimer m_longPressTimer;
    bool m_bLongPressFired {false}; // long press reported while still held
    bool m_bPressed {false};        // debounced logical state (true = pressed)
    uint64_t m_nPressStartTime {0};

    eventCallback m_pEventCB {nullptr};
    void* m_pEventCtx {nullptr};

    static std::map<uint, Button::Shared> sm_mapButtons;
};
