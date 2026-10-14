/*
 * Time manager for wall-clock validity and time-zone offset state.
 *
 * Copyright (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

#include "pico/time.h"
#include "pico/sync.h"

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

    // Identifies which source last successfully set the wall clock.
    enum class TimeSource
    {
        Unknown,
        Gps,
        Ntp,
    };

    static Shared GetInstance();
    static void InitializeSingleton(std::string timeZoneName = "UTC");

    static bool ResolveTimeZoneOffset(const std::string& timeZoneName, std::time_t whenUtc, float& offsetHours, bool* pIsDst = nullptr);
    static bool IsWallClockValid();
    static uint64_t CurrentEpochSeconds();
    static bool IsGpsTimeDateWithinOneSecond(const std::string& gpsTime, const std::string& gpsDate);
    static std::string FormatCurrentTimestamp();
    static std::string FormatCurrentTimeHMS();
    static std::string FormatCurrentTimeUTC();
    static std::string FormatCurrentDate();
    static std::string FormatCurrentDateUTC();

    static bool SetTimeFromNtp(uint32_t timeoutMs = 10000);
    static void EnableNtpAutoRetry(uint32_t retryIntervalMs = 60000, uint32_t timeoutMs = 10000);
    static bool AttemptNtpTimeSync();
    static bool SetTimeFromGps(const std::string& gpsTime, const std::string& gpsDate);
    static TimeSource GetTimeSource();
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
    void enableNtpAutoRetry(uint32_t retryIntervalMs, uint32_t timeoutMs);
    bool attemptNtpTimeSync();
    bool setTimeFromGps(const std::string& gpsTime, const std::string& gpsDate);
    TimeSource getTimeSource() const;
    bool refreshTimeZoneOffset(std::time_t whenUtc = 0);
    bool isValid() const;
    bool hasTimeZoneOffset() const;
    float timeZoneOffsetHours() const;
    bool isDst() const;
    const std::string& timeZoneName() const;
    void setTimeZoneName(std::string timeZoneName);
    std::string formatCurrentTimeHMS() const;
    std::string formatCurrentTimeUTC() const;
    std::string formatCurrentDate() const;
    std::string formatCurrentDateUTC() const;

    static Shared sm_spTimeMgr;

    std::string m_timeZoneName;
    std::string m_timeZoneAbbrev;
    float m_timeZoneOffsetHours;
    bool m_bIsDst;
    bool m_bHasTimeZoneOffset;

    bool m_bNtpAutoRetryEnabled;
    uint32_t m_ntpRetryIntervalMs;
    uint32_t m_ntpTimeoutMs;
    absolute_time_t m_nextNtpAttemptTime;
    TimeSource m_timeSource {TimeSource::Unknown};
};
