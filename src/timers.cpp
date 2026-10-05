/*
 * Timer utilities.
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
 */

#include "timers.h"

DelayedRepeatingTimer::DelayedRepeatingTimer(uint32_t delayMs, uint32_t intervalMs, std::function<void()> callback, alarm_pool_t* pAlarmPool)
    : m_delayMs(delayMs),
      m_intervalMs(intervalMs),
      m_callback(std::move(callback)),
      m_delayAlarmId(0),
      m_repeatingTimer {},
      m_bRepeatingActive(false),
      m_bRunning(false)
{
    if (nullptr != pAlarmPool)
    {
        m_pAlarmPool = pAlarmPool;
    }
    else
    {
        m_pAlarmPool = alarm_pool_get_default();
    }
}

DelayedRepeatingTimer::~DelayedRepeatingTimer()
{
    Stop();
}

void DelayedRepeatingTimer::Start()
{
    Stop();

    m_bRunning = true;
    m_delayAlarmId = alarm_pool_add_alarm_in_ms(m_pAlarmPool, m_delayMs, &DelayedRepeatingTimer::delayAlarmCallback, this, true);
    if (m_delayAlarmId <= 0)
    {
        m_bRunning = false;
    }
}

void DelayedRepeatingTimer::Stop()
{
    if (m_delayAlarmId > 0)
    {
        alarm_pool_cancel_alarm(m_pAlarmPool, m_delayAlarmId);
        m_delayAlarmId = 0;
    }

    if (m_bRepeatingActive)
    {
        cancel_repeating_timer(&m_repeatingTimer);
        m_bRepeatingActive = false;
    }

    m_bRunning = false;
}

bool DelayedRepeatingTimer::IsRunning() const
{
    return m_bRunning;
}

int64_t DelayedRepeatingTimer::delayAlarmCallback(alarm_id_t alarmId, void* pUserData)
{
    auto* pTimer = static_cast<DelayedRepeatingTimer*>(pUserData);
    if (pTimer == nullptr)
    {
        return 0;
    }
    return pTimer->onDelayAlarm(alarmId);
}

bool DelayedRepeatingTimer::repeatingTimerCallback(repeating_timer* pRepeatingTimer)
{
    if (pRepeatingTimer == nullptr)
    {
        return false;
    }

    auto* pTimer = static_cast<DelayedRepeatingTimer*>(pRepeatingTimer->user_data);
    if (pTimer == nullptr)
    {
        return false;
    }

    return pTimer->onRepeatingTick();
}

int64_t DelayedRepeatingTimer::onDelayAlarm(alarm_id_t alarmId)
{
    (void)alarmId;
    m_delayAlarmId = 0;

    if (!m_bRunning)
    {
        return 0;
    }

    if (m_callback)
    {
        m_callback();
    }

    if (m_intervalMs == 0)
    {
        m_bRunning = false;
        return 0;
    }

    const int64_t intervalUs = -static_cast<int64_t>(m_intervalMs) * 1000;
    m_bRepeatingActive = alarm_pool_add_repeating_timer_us(m_pAlarmPool,
                                                           intervalUs,
                                                           &DelayedRepeatingTimer::repeatingTimerCallback,
                                                           this,
                                                           &m_repeatingTimer);
    if (!m_bRepeatingActive)
    {
        m_bRunning = false;
    }

    return 0;
}

bool DelayedRepeatingTimer::onRepeatingTick()
{
    if (!m_bRunning)
    {
        m_bRepeatingActive = false;
        return false;
    }

    if (m_callback)
    {
        m_callback();
    }

    return m_bRunning;
}

//
// AlarmaTimer implementation
//
AlarmTimer::AlarmTimer(std::function<void()> callback, alarm_pool_t* pAlarmPool)
    : m_callback(std::move(callback)),
      m_alarmId(0),
      m_bRunning(false)
{
    if (nullptr != pAlarmPool)
    {
        m_pAlarmPool = pAlarmPool;
    }
    else
    {
        m_pAlarmPool = alarm_pool_get_default();
    }
}

AlarmTimer::~AlarmTimer()
{
    Stop();
}

void AlarmTimer::Start(uint32_t delayMs)
{
    Stop();
    m_bRunning = true;
    m_alarmId = alarm_pool_add_alarm_in_ms(m_pAlarmPool, delayMs, &AlarmTimer::alarmCallback, this, true);
    if (m_alarmId <= 0)
    {
        m_bRunning = false;
    }
}

void AlarmTimer::Stop()
{
    if (m_alarmId > 0)
    {
        alarm_pool_cancel_alarm(m_pAlarmPool, m_alarmId);
        m_alarmId = 0;
    }
    m_bRunning = false;
}

bool AlarmTimer::IsRunning() const
{
    return m_bRunning;
}

int64_t AlarmTimer::alarmCallback(alarm_id_t alarmId, void* pUserData)
{
    auto* pTimer = static_cast<AlarmTimer*>(pUserData);
    if (pTimer == nullptr)
    {
        return 0;
    }
    return pTimer->onAlarm(alarmId);
}

int64_t AlarmTimer::onAlarm(alarm_id_t alarmId)
{
    (void)alarmId;
    m_alarmId = 0;
    m_bRunning = false;

    if (m_callback)
    {
        m_callback();
    }

    return 0;
}
