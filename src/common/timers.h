/*
 * Timer utilities.
 *
 * Copyright (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <functional>
#include <memory>

#include "pico/time.h"
#include "pico/types.h"

// DelayedRepeatingTimer is a utility class that provides a mechanism to execute a callback
// function after a specified delay and then repeatedly at a specified interval.
// It is useful for scheduling periodic tasks in applications. The timer can be started and
// stopped, and it provides a method to check if it is currently running.
//
class DelayedRepeatingTimer
{
public:
    typedef std::shared_ptr<DelayedRepeatingTimer> Shared;

    DelayedRepeatingTimer(uint32_t delayMs, uint32_t intervalMs, std::function<void()> callback, alarm_pool_t* pAlarmPool = nullptr);
    ~DelayedRepeatingTimer();

    void Start();
    void Stop();
    bool IsRunning() const;

private:
    static int64_t delayAlarmCallback(alarm_id_t alarmId, void* pUserData);
    static bool repeatingTimerCallback(repeating_timer* pRepeatingTimer);

    int64_t onDelayAlarm(alarm_id_t alarmId);
    bool onRepeatingTick();

    uint32_t m_delayMs;
    uint32_t m_intervalMs;
    std::function<void()> m_callback;
    alarm_pool_t* m_pAlarmPool;
    alarm_id_t m_delayAlarmId;
    repeating_timer m_repeatingTimer;
    bool m_bRepeatingActive;
    bool m_bRunning;
};

// AlarmTimer is a utility class that executes a callback once at a specific future time.
//
class AlarmTimer
{
public:
    typedef std::shared_ptr<AlarmTimer> Shared;

    explicit AlarmTimer(std::function<void()> callback, alarm_pool_t* pAlarmPool = nullptr);
    ~AlarmTimer();

    void Start(uint32_t delayMs);
    void Stop();
    bool IsRunning() const;

private:
    static int64_t alarmCallback(alarm_id_t alarmId, void* pUserData);

    int64_t onAlarm(alarm_id_t alarmId);

    std::function<void()> m_callback;
    alarm_pool_t* m_pAlarmPool;
    alarm_id_t m_alarmId;
    bool m_bRunning;
};
