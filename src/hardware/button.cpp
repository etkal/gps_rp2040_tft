/*
 * Button class
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

#include "button.h"

#include <iostream>
#include "hardware/gpio.h"


std::map<uint, Button::Shared> Button::sm_mapButtons;

Button::Button(uint nButtonPin, uint nDebounceMs, uint nPressMs, uint nLongPressMs)
    : m_nPin(nButtonPin),
      m_nDebounceMs(nDebounceMs),
      m_nPressMs(nPressMs),
      m_nLongPressMs(nLongPressMs),
      m_debounceTimer([this]() {
          onDebounceTimer();
      }),
      m_longPressTimer([this]() {
          onLongPressTimer();
      })
{
}

void Button::Initialize()
{
    gpio_init(m_nPin);
    gpio_set_dir(m_nPin, GPIO_IN);
    gpio_pull_up(m_nPin);
    sleep_ms(2);                    // let the pull-up settle, otherwise the pin can read low and mask the first press
    m_bPressed = !gpio_get(m_nPin); // establish initial resting state (active low)
    gpio_set_irq_enabled_with_callback(m_nPin, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &Button::irqHandler);
    sm_mapButtons[m_nPin] = shared_from_this();
}

void Button::SetEventCallback(void* pCtx, eventCallback pCB)
{
    m_pEventCtx = pCtx;
    m_pEventCB = pCB;
}

Button::Shared Button::GetButton(uint nButtonPin)
{
    if (sm_mapButtons.find(nButtonPin) != sm_mapButtons.end())
    {
        return sm_mapButtons[nButtonPin];
    }
    return nullptr;
}

void Button::irqHandler(uint nPin, uint32_t events)
{
    (void)events; // exact edge type is irrelevant; we re-check the settled level after debouncing
    auto spButton = GetButton(nPin);
    if (!spButton)
    {
        return; // Button not found, ignore the interrupt
    }

    // Any edge activity means the line is bouncing; (re)start the quiet-time
    // debounce timer and only inspect the settled level once it fires.
    spButton->m_debounceTimer.Start(spButton->m_nDebounceMs);
}

void Button::onDebounceTimer()
{
    bool bPressedNow = !gpio_get(m_nPin); // active low
    if (bPressedNow == m_bPressed)
    {
        return; // Bounced back to the previous stable state, not a real transition
    }
    m_bPressed = bPressedNow;

    if (bPressedNow)
    {
        m_nPressStartTime = time_us_64();
        m_bLongPressFired = false;
        m_longPressTimer.Start(m_nLongPressMs);
    }
    else if (m_bLongPressFired)
    {
        m_bLongPressFired = false; // long press already reported; ignore the release
    }
    else if (m_pEventCB)
    {
        m_longPressTimer.Stop();
        uint64_t pressDuration = (time_us_64() - m_nPressStartTime) / 1000; // ms

        if (pressDuration >= m_nLongPressMs)
        {
            m_pEventCB(m_pEventCtx, ButtonEvent::LongPress);
        }
        else if (pressDuration >= m_nPressMs)
        {
            m_pEventCB(m_pEventCtx, ButtonEvent::Press);
        }
        else
        {
            m_pEventCB(m_pEventCtx, ButtonEvent::Tap);
        }
    }
}

void Button::onLongPressTimer()
{
    if (!m_bPressed || m_bLongPressFired)
    {
        return;
    }
    m_bLongPressFired = true;
    if (m_pEventCB)
    {
        m_pEventCB(m_pEventCtx, ButtonEvent::LongPress);
    }
}
