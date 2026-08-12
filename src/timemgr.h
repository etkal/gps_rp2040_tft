/*
 * Time manager for wall-clock validity and time-zone offset state.
 *
 * (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

#include "pico/time.h"

// TimeMgr is a singleton class that manages wall-clock validity and time-zone offset state.
// It provides methods to set the time from NTP or GPS, check if the wall-clock is valid,
// get the current epoch seconds, and format timestamps. It also allows for resolving time
// zone offsets and checking if the current time is in daylight saving time (DST).
// The class uses a shared pointer for its singleton instance and provides thread-safe
// access to its methods.
//
class TimeMgr
{
public:
    typedef std::shared_ptr<TimeMgr> Shared;

    static Shared GetInstance();
    static void InitializeSingleton(std::string timeZoneName = "UTC");

    static bool ResolveTimeZoneOffset(const std::string& timeZoneName, std::time_t whenUtc, float& offsetHours, bool* pIsDst = nullptr);
    static bool IsWallClockValid();
    static uint64_t CurrentEpochSeconds();
    static bool IsGpsTimeDateWithinOneSecond(const std::string& gpsTime, const std::string& gpsDate);
    static std::string FormatCurrentTimestamp();
    static std::string FormatCurrentTimeHMS();
    static void LogInfo(const std::string& message);

    static bool SetTimeFromNtp(uint32_t timeoutMs = 10000);
    static bool SetTimeFromGps(const std::string& gpsTime, const std::string& gpsDate);
    static bool RefreshTimeZoneOffset(std::time_t whenUtc = 0);
    static bool IsValid();
    static bool HasTimeZoneOffset();
    static float TimeZoneOffsetHours();
    static bool IsDst();
    static const std::string& TimeZoneName();
    static void SetTimeZoneName(std::string timeZoneName);

private:
    explicit TimeMgr(std::string timeZoneName = "UTC");

    bool setTimeFromNtp(uint32_t timeoutMs = 10000);
    bool setTimeFromGps(const std::string& gpsTime, const std::string& gpsDate);
    bool refreshTimeZoneOffset(std::time_t whenUtc = 0);
    bool isValid() const;
    bool hasTimeZoneOffset() const;
    float timeZoneOffsetHours() const;
    bool isDst() const;
    const std::string& timeZoneName() const;
    void setTimeZoneName(std::string timeZoneName);

    static Shared sm_spTimeMgr;

    std::string m_timeZoneName;
    float m_timeZoneOffsetHours;
    bool m_isDst;
    bool m_hasTimeZoneOffset;
};

// Helper function to log messages with TimeMgr context
inline void LogInfo(const std::string& message)
{
    TimeMgr::LogInfo(message);
}

// DelayedRepeatingTimer is a utility class that provides a mechanism to execute a callback
// function after a specified delay and then repeatedly at a specified interval.
// It is useful for scheduling periodic tasks in applications. The timer can be started and
// stopped, and it provides a method to check if it is currently running.
//
class DelayedRepeatingTimer
{
public:
    DelayedRepeatingTimer(uint32_t delayMs, uint32_t intervalMs, std::function<void()> callback);
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
    alarm_id_t m_delayAlarmId;
    repeating_timer m_repeatingTimer;
    bool m_repeatingActive;
    bool m_running;
};

// AlarmTimer is a utility class that executes a callback once at a specific future time.
class AlarmTimer
{
public:
    explicit AlarmTimer(std::function<void()> callback);
    ~AlarmTimer();

    void Start(uint32_t delayMs);
    void Stop();
    bool IsRunning() const;

private:
    static int64_t alarmCallback(alarm_id_t alarmId, void* pUserData);

    int64_t onAlarm(alarm_id_t alarmId);

    std::function<void()> m_callback;
    alarm_id_t m_alarmId;
    bool m_running;
};
