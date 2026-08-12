/*
 * Time manager for wall-clock validity and time-zone offset state.
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "timemgr.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <chrono>
#include <sys/time.h>

#include "pico/stdlib.h"
#include "pico/aon_timer.h"

#ifndef TIMEMGR_ENABLE_NTP
#define TIMEMGR_ENABLE_NTP 0
#endif

#if TIMEMGR_ENABLE_NTP
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#endif

#include <utility>

#ifndef USE_DST
#define USE_DST AUTOMATIC
#endif
#define STRINGIFY_VALUE(x) #x
#define STRINGIFY(x)       STRINGIFY_VALUE(x)

namespace
{
    constexpr uint64_t validEpochThresholdSec = 1700000000ULL;

#if TIMEMGR_ENABLE_NTP
    constexpr uint32_t ntpPort = 123;
    constexpr uint32_t ntpPacketSize = 48;
    constexpr uint64_t ntpEpochDeltaSeconds = 2208988800ULL;
#endif

    enum class DstRule
    {
        None,
        NorthernHemisphere,
        European,
        SouthernHemisphere,
    };

    enum class DstOverride
    {
        Automatic,
        Off,
        On,
    };

    std::string trim_copy(const std::string& value)
    {
        const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
        const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                             return std::isspace(ch) != 0;
                         }).base();
        return (begin >= end) ? std::string() : std::string(begin, end);
    }

    std::string to_lower_copy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    int day_of_week(int year, int month, int day)
    {
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        year -= (month < 3);
        return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
    }

    int nth_sunday_day(int year, int month, int nth)
    {
        const int first_day = 1 + ((7 + (0 - day_of_week(year, month, 1))) % 7);
        return first_day + (nth - 1) * 7;
    }

    int last_sunday_day(int year, int month)
    {
        const int last_day = 31;
        for (int day = last_day; day >= 1; --day)
        {
            if (day_of_week(year, month, day) == 0)
            {
                return day;
            }
        }
        return 1;
    }

    DstOverride parse_dst_override()
    {
        const std::string value = to_lower_copy(STRINGIFY(USE_DST));
        if (value == "yes" || value == "on" || value == "true")
        {
            return DstOverride::On;
        }
        if (value == "no" || value == "off" || value == "false")
        {
            return DstOverride::Off;
        }
        return DstOverride::Automatic;
    }

    bool is_dst_active(std::time_t whenUtc, DstRule rule)
    {
        std::tm utcTm {};
        if (gmtime_r(&whenUtc, &utcTm) == nullptr)
        {
            return false;
        }

        const int year = utcTm.tm_year + 1900;
        const int month = utcTm.tm_mon + 1;
        const int day = utcTm.tm_mday;

        switch (rule)
        {
        case DstRule::NorthernHemisphere:
            if (month < 3 || month > 11)
            {
                return false;
            }
            if (month > 3 && month < 11)
            {
                return true;
            }
            if (month == 3)
            {
                return day >= nth_sunday_day(year, 3, 2);
            }
            return day < nth_sunday_day(year, 11, 1);

        case DstRule::European:
            if (month < 3 || month > 10)
            {
                return false;
            }
            if (month > 3 && month < 10)
            {
                return true;
            }
            if (month == 3)
            {
                return day >= last_sunday_day(year, 3);
            }
            return day < last_sunday_day(year, 10);

        case DstRule::SouthernHemisphere:
            if (month < 10 && month > 4)
            {
                return false;
            }
            if (month > 10 || month < 4)
            {
                return true;
            }
            if (month == 10)
            {
                return day >= nth_sunday_day(year, 10, 1);
            }
            return day < nth_sunday_day(year, 4, 1);

        default:
            return false;
        }
    }

    bool resolve_dst_for_rule(DstOverride override, std::time_t whenUtc, DstRule rule)
    {
        if (override == DstOverride::On)
        {
            return true;
        }
        if (override == DstOverride::Off)
        {
            return false;
        }
        return is_dst_active(whenUtc, rule);
    }

    bool parse_numeric_offset(const std::string& text, int& totalMinutesOut)
    {
        std::string value = trim_copy(text);
        if (value.empty())
        {
            return false;
        }

        bool negative = false;
        if (value[0] == '+' || value[0] == '-')
        {
            negative = value[0] == '-';
            value = value.substr(1);
        }

        const auto separator = value.find(':');
        int hours = 0;
        int minutes = 0;
        bool parsed = false;

        if (separator == std::string::npos)
        {
            if (value.empty())
            {
                return false;
            }
            const char* start = value.c_str();
            char* end = nullptr;
            const long parsedValue = std::strtol(start, &end, 10);
            if (end != start && *end == '\0')
            {
                hours = static_cast<int>(parsedValue);
                minutes = 0;
                parsed = true;
            }
        }
        else
        {
            const std::string hoursText = value.substr(0, separator);
            const std::string minutesText = value.substr(separator + 1);
            if (!hoursText.empty() && !minutesText.empty())
            {
                const char* hoursStart = hoursText.c_str();
                char* hoursEnd = nullptr;
                const long parsedHours = std::strtol(hoursStart, &hoursEnd, 10);
                const char* minutesStart = minutesText.c_str();
                char* minutesEnd = nullptr;
                const long parsedMinutes = std::strtol(minutesStart, &minutesEnd, 10);
                if (hoursEnd != hoursStart && *hoursEnd == '\0' && minutesEnd != minutesStart && *minutesEnd == '\0')
                {
                    hours = static_cast<int>(parsedHours);
                    minutes = static_cast<int>(parsedMinutes);
                    parsed = true;
                }
            }
        }

        if (!parsed || hours < 0 || hours > 24 || minutes < 0 || minutes > 59)
        {
            return false;
        }

        totalMinutesOut = (negative ? -1 : 1) * (hours * 60 + minutes);
        return true;
    }

    bool parse_fixed_unsigned(const std::string& text, size_t start, size_t length, unsigned int& valueOut)
    {
        if (text.size() < start + length)
        {
            return false;
        }

        unsigned int value = 0;
        for (size_t index = 0; index < length; ++index)
        {
            const unsigned char ch = static_cast<unsigned char>(text[start + index]);
            if (!std::isdigit(ch))
            {
                return false;
            }
            value = value * 10 + static_cast<unsigned int>(ch - '0');
        }

        valueOut = value;
        return true;
    }

    bool parse_gprmc_time(const std::string& timeText, int& hourOut, int& minuteOut, int& secondOut)
    {
        unsigned int hour = 0;
        unsigned int minute = 0;
        unsigned int second = 0;
        if (!parse_fixed_unsigned(timeText, 0, 2, hour) || !parse_fixed_unsigned(timeText, 2, 2, minute) ||
            !parse_fixed_unsigned(timeText, 4, 2, second))
        {
            return false;
        }

        hourOut = static_cast<int>(hour);
        minuteOut = static_cast<int>(minute);
        secondOut = static_cast<int>(second);
        return hourOut < 24 && minuteOut < 60 && secondOut < 60;
    }

    bool parse_gprmc_date(const std::string& dateText, int& yearOut, int& monthOut, int& dayOut)
    {
        unsigned int day = 0;
        unsigned int month = 0;
        unsigned int year = 0;
        if (!parse_fixed_unsigned(dateText, 0, 2, day) || !parse_fixed_unsigned(dateText, 2, 2, month) ||
            !parse_fixed_unsigned(dateText, 4, 2, year))
        {
            return false;
        }

        if (year >= 70)
        {
            yearOut = 1900 + static_cast<int>(year);
        }
        else
        {
            yearOut = 2000 + static_cast<int>(year);
        }

        monthOut = static_cast<int>(month);
        dayOut = static_cast<int>(day);
        return monthOut >= 1 && monthOut <= 12 && dayOut >= 1 && dayOut <= 31;
    }

    bool is_leap_year(int year)
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    bool is_valid_ymd(int year, int month, int day)
    {
        static const int monthLengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12)
        {
            return false;
        }

        int maxDay = monthLengths[month - 1];
        if (month == 2 && is_leap_year(year))
        {
            maxDay = 29;
        }
        return day >= 1 && day <= maxDay;
    }

    std::time_t utc_time_from_ymdhms(int year, int month, int day, int hour, int minute, int second)
    {
        const int adjustedYear = year - (month <= 2 ? 1 : 0);
        const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
        const unsigned int yoe = static_cast<unsigned int>(adjustedYear - era * 400);
        const unsigned int monthIndex = static_cast<unsigned int>(month + (month > 2 ? -3 : 9));
        const unsigned int dayOfYear = (153 * monthIndex + 2) / 5 + static_cast<unsigned int>(day) - 1;
        const unsigned int dayOfEra = yoe * 365 + yoe / 4 - yoe / 100 + dayOfYear;
        const int64_t daysSinceEpoch = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
        const int64_t secondsSinceEpoch = daysSinceEpoch * 86400 + static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 +
                                          second;
        return static_cast<std::time_t>(secondsSinceEpoch);
    }

#if TIMEMGR_ENABLE_NTP
    struct NtpQueryContext
    {
        volatile bool bDnsReady;
        volatile bool bDnsFailed;
        volatile bool bResponseReady;
        volatile bool bResponseFailed;
        ip_addr_t serverAddr;
        uint32_t seconds1900;
        struct udp_pcb* pUdp;
    };

    void ntp_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port)
    {
        (void)pcb;
        (void)addr;
        (void)port;

        auto* pCtx = static_cast<NtpQueryContext*>(arg);
        if (pCtx == nullptr)
        {
            if (p != nullptr)
            {
                pbuf_free(p);
            }
            return;
        }

        if (p == nullptr || p->tot_len < ntpPacketSize)
        {
            pCtx->bResponseFailed = true;
            if (p != nullptr)
            {
                pbuf_free(p);
            }
            return;
        }

        std::array<uint8_t, ntpPacketSize> packet {};
        if (pbuf_copy_partial(p, packet.data(), packet.size(), 0) != ntpPacketSize)
        {
            pCtx->bResponseFailed = true;
            pbuf_free(p);
            return;
        }

        pbuf_free(p);
        pCtx->seconds1900 = (static_cast<uint32_t>(packet[40]) << 24) | (static_cast<uint32_t>(packet[41]) << 16) |
                            (static_cast<uint32_t>(packet[42]) << 8) | static_cast<uint32_t>(packet[43]);
        pCtx->bResponseReady = true;
    }

    void ntp_dns_found_callback(const char* name, const ip_addr_t* ipaddr, void* arg)
    {
        (void)name;
        auto* pCtx = static_cast<NtpQueryContext*>(arg);
        if (pCtx == nullptr)
        {
            return;
        }

        if (ipaddr == nullptr)
        {
            pCtx->bDnsFailed = true;
            return;
        }

        pCtx->serverAddr = *ipaddr;
        pCtx->bDnsReady = true;
    }

    bool wait_for_flag(const volatile bool& bDoneFlag, const volatile bool& bFailFlag, uint32_t timeoutMs)
    {
        const absolute_time_t timeoutAt = make_timeout_time_ms(timeoutMs);
        while (true)
        {
            if (bDoneFlag)
            {
                return true;
            }
            if (bFailFlag)
            {
                return false;
            }
            if (absolute_time_diff_us(get_absolute_time(), timeoutAt) <= 0)
            {
                return false;
            }
            sleep_ms(10);
        }
    }
#endif

    std::string format_uptime_timestamp()
    {
        const uint64_t nowUs = time_us_64();
        const uint64_t totalMs = nowUs / 1000;
        const uint64_t ms = totalMs % 1000;
        const uint64_t totalSec = totalMs / 1000;
        const uint64_t sec = totalSec % 60;
        const uint64_t min = (totalSec / 60) % 60;
        const uint64_t hour = totalSec / 3600;

        std::ostringstream out;
        out << "UP " << std::setfill('0') << std::setw(2) << hour << ':' << std::setw(2) << min << ':' << std::setw(2) << sec << '.'
            << std::setw(3) << ms;
        return out.str();
    }
} // namespace

TimeMgr::Shared TimeMgr::sm_spTimeMgr;

TimeMgr::Shared TimeMgr::GetInstance()
{
    if (!sm_spTimeMgr)
    {
        sm_spTimeMgr = Shared(new TimeMgr("UTC"));
    }
    return sm_spTimeMgr;
}

void TimeMgr::InitializeSingleton(std::string timeZoneName)
{
    if (!sm_spTimeMgr)
    {
        sm_spTimeMgr = Shared(new TimeMgr(std::move(timeZoneName)));
        return;
    }

    sm_spTimeMgr->setTimeZoneName(std::move(timeZoneName));
}

bool TimeMgr::ResolveTimeZoneOffset(const std::string& timeZoneName, std::time_t whenUtc, float& offsetHours, bool* pIsDst)
{
    std::string zone = trim_copy(timeZoneName);
    if (zone.empty())
    {
        offsetHours = 0.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }

    const std::string lower = to_lower_copy(zone);
    if (lower == "utc" || lower == "gmt")
    {
        offsetHours = 0.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }

    if (lower.rfind("utc", 0) == 0 || lower.rfind("gmt", 0) == 0)
    {
        std::string suffix = lower.substr(3);
        if (!suffix.empty())
        {
            int totalMinutes = 0;
            if (parse_numeric_offset(suffix, totalMinutes))
            {
                offsetHours = static_cast<float>(totalMinutes) / 60.0f;
                if (pIsDst != nullptr)
                {
                    *pIsDst = false;
                }
                return true;
            }
        }
    }

    const DstOverride dstOverride = parse_dst_override();
    const bool isDst = (whenUtc > 0) ? resolve_dst_for_rule(dstOverride, whenUtc, DstRule::NorthernHemisphere) : false;

    if (lower == "america/new_york" || lower == "america/toronto" || lower == "america/halifax")
    {
        offsetHours = isDst ? -4.0f : -5.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = isDst;
        }
        return true;
    }
    if (lower == "america/chicago" || lower == "america/mexico_city")
    {
        offsetHours = isDst ? -5.0f : -6.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = isDst;
        }
        return true;
    }
    if (lower == "america/denver")
    {
        offsetHours = isDst ? -6.0f : -7.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = isDst;
        }
        return true;
    }
    if (lower == "america/los_angeles" || lower == "america/vancouver")
    {
        offsetHours = isDst ? -7.0f : -8.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = isDst;
        }
        return true;
    }
    if (lower == "america/phoenix")
    {
        offsetHours = -7.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }
    if (lower == "europe/london" || lower == "europe/ireland" || lower == "europe/dublin")
    {
        const bool dst = (whenUtc > 0) ? resolve_dst_for_rule(dstOverride, whenUtc, DstRule::European) : false;
        offsetHours = dst ? 1.0f : 0.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = dst;
        }
        return true;
    }
    if (lower == "europe/paris" || lower == "europe/berlin" || lower == "europe/amsterdam" || lower == "europe/rome" ||
        lower == "europe/madrid")
    {
        const bool dst = (whenUtc > 0) ? resolve_dst_for_rule(dstOverride, whenUtc, DstRule::European) : false;
        offsetHours = dst ? 2.0f : 1.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = dst;
        }
        return true;
    }
    if (lower == "asia/tokyo" || lower == "asia/seoul" || lower == "asia/shanghai" || lower == "asia/hong_kong")
    {
        offsetHours = 9.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }
    if (lower == "asia/kolkata")
    {
        offsetHours = 5.5f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }
    if (lower == "australia/sydney" || lower == "australia/melbourne")
    {
        const bool dst = (whenUtc > 0) ? resolve_dst_for_rule(dstOverride, whenUtc, DstRule::SouthernHemisphere) : false;
        offsetHours = dst ? 11.0f : 10.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = dst;
        }
        return true;
    }

    int totalMinutes = 0;
    if (parse_numeric_offset(zone, totalMinutes))
    {
        offsetHours = static_cast<float>(totalMinutes) / 60.0f;
        if (pIsDst != nullptr)
        {
            *pIsDst = false;
        }
        return true;
    }

    offsetHours = 0.0f;
    if (pIsDst != nullptr)
    {
        *pIsDst = false;
    }
    return false;
}

bool TimeMgr::IsWallClockValid()
{
    return static_cast<uint64_t>(std::time(nullptr)) >= validEpochThresholdSec;
}

uint64_t TimeMgr::CurrentEpochSeconds()
{
    return static_cast<uint64_t>(std::time(nullptr));
}

bool TimeMgr::IsGpsTimeDateWithinOneSecond(const std::string& gpsTime, const std::string& gpsDate)
{
    if (!IsWallClockValid())
    {
        return false;
    }

    int hour = 0;
    int minute = 0;
    int second = 0;
    int year = 0;
    int month = 0;
    int day = 0;

    if (!parse_gprmc_time(gpsTime, hour, minute, second) || !parse_gprmc_date(gpsDate, year, month, day) || !is_valid_ymd(year, month, day))
    {
        return false;
    }

    const std::time_t gpsUtc = utc_time_from_ymdhms(year, month, day, hour, minute, second);
    const std::time_t nowUtc = std::time(nullptr);
    const int64_t diff = static_cast<int64_t>(nowUtc) - static_cast<int64_t>(gpsUtc);
    return diff >= -1 && diff <= 1;
}

std::string TimeMgr::FormatCurrentTimestamp()
{
    if (!IsWallClockValid())
    {
        return format_uptime_timestamp();
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow {};
    localtime_r(&nowTimeT, &tmNow);

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tmNow.tm_hour << ':' << std::setw(2) << tmNow.tm_min << ':' << std::setw(2) << tmNow.tm_sec
        << '.' << std::setw(3) << ms.count();
    return oss.str();
}

std::string TimeMgr::FormatCurrentTimeHMS()
{
    if (!IsWallClockValid())
    {
        return "";
    }

    const std::time_t now = std::time(nullptr);
    std::tm tmNow {};
    localtime_r(&now, &tmNow);

    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmNow);
    return std::string(buf);
}

void TimeMgr::LogInfo(const std::string& message)
{
    std::cout << '[' << FormatCurrentTimestamp() << "] " << message << std::endl;
}

TimeMgr::TimeMgr(std::string timeZoneName)
    : m_timeZoneName(std::move(timeZoneName)),
      m_timeZoneOffsetHours(0.0f),
      m_isDst(false),
      m_hasTimeZoneOffset(false)
{
}

bool TimeMgr::SetTimeFromNtp(uint32_t timeoutMs)
{
    return GetInstance()->setTimeFromNtp(timeoutMs);
}

bool TimeMgr::SetTimeFromGps(const std::string& gpsTime, const std::string& gpsDate)
{
    return GetInstance()->setTimeFromGps(gpsTime, gpsDate);
}

bool TimeMgr::RefreshTimeZoneOffset(std::time_t whenUtc)
{
    return GetInstance()->refreshTimeZoneOffset(whenUtc);
}

bool TimeMgr::IsValid()
{
    return GetInstance()->isValid();
}

bool TimeMgr::HasTimeZoneOffset()
{
    return GetInstance()->hasTimeZoneOffset();
}

float TimeMgr::TimeZoneOffsetHours()
{
    return GetInstance()->timeZoneOffsetHours();
}

bool TimeMgr::IsDst()
{
    return GetInstance()->isDst();
}

const std::string& TimeMgr::TimeZoneName()
{
    return GetInstance()->timeZoneName();
}

void TimeMgr::SetTimeZoneName(std::string timeZoneName)
{
    GetInstance()->setTimeZoneName(std::move(timeZoneName));
}

bool TimeMgr::setTimeFromNtp(uint32_t timeoutMs)
{
    (void)timeoutMs;

    if (IsWallClockValid())
    {
        return true;
    }

#if !TIMEMGR_ENABLE_NTP
    return false;
#else
    NtpQueryContext ctx {};

    cyw43_arch_lwip_begin();
    err_t dnsErr = dns_gethostbyname("pool.ntp.org", &ctx.serverAddr, ntp_dns_found_callback, &ctx);
    cyw43_arch_lwip_end();

    if (dnsErr == ERR_OK)
    {
        ctx.bDnsReady = true;
    }
    else if (dnsErr != ERR_INPROGRESS)
    {
        return false;
    }

    if (!wait_for_flag(ctx.bDnsReady, ctx.bDnsFailed, timeoutMs / 2))
    {
        return false;
    }

    cyw43_arch_lwip_begin();
    ctx.pUdp = udp_new_ip_type(IP_GET_TYPE(ctx.serverAddr));
    if (ctx.pUdp != nullptr)
    {
        udp_recv(ctx.pUdp, ntp_recv_callback, &ctx);

        std::array<uint8_t, ntpPacketSize> request {};
        request[0] = 0x1b;

        struct pbuf* pPacket = pbuf_alloc(PBUF_TRANSPORT, request.size(), PBUF_RAM);
        if (pPacket != nullptr)
        {
            std::memcpy(pPacket->payload, request.data(), request.size());
            err_t sendErr = udp_sendto(ctx.pUdp, pPacket, &ctx.serverAddr, ntpPort);
            pbuf_free(pPacket);
            if (sendErr != ERR_OK)
            {
                ctx.bResponseFailed = true;
            }
        }
        else
        {
            ctx.bResponseFailed = true;
        }
    }
    else
    {
        ctx.bResponseFailed = true;
    }
    cyw43_arch_lwip_end();

    const bool bReceived = wait_for_flag(ctx.bResponseReady, ctx.bResponseFailed, timeoutMs);

    cyw43_arch_lwip_begin();
    if (ctx.pUdp != nullptr)
    {
        udp_remove(ctx.pUdp);
        ctx.pUdp = nullptr;
    }
    cyw43_arch_lwip_end();

    if (!bReceived || ctx.seconds1900 <= ntpEpochDeltaSeconds)
    {
        return false;
    }

    const time_t unixSeconds = static_cast<time_t>(ctx.seconds1900 - ntpEpochDeltaSeconds);
    timeval tv {};
    tv.tv_sec = unixSeconds;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        LogInfo("Failed to set time from NTP: " + std::string(std::strerror(errno)));
        return false;
    }

    aon_timer_start_with_timeofday();
    refreshTimeZoneOffset(unixSeconds);
    return true;
#endif
}

bool TimeMgr::setTimeFromGps(const std::string& gpsTime, const std::string& gpsDate)
{
    int hour = 0;
    int minute = 0;
    int second = 0;
    int year = 0;
    int month = 0;
    int day = 0;

    if (!parse_gprmc_time(gpsTime, hour, minute, second) || !parse_gprmc_date(gpsDate, year, month, day) || !is_valid_ymd(year, month, day))
    {
        LogInfo("Failed to parse GPS time/date: time='" + gpsTime + "', date='" + gpsDate + "'");
        return false;
    }

    const std::time_t gpsUtc = utc_time_from_ymdhms(year, month, day, hour, minute, second);
    timeval tv {};
    tv.tv_sec = gpsUtc;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        LogInfo("Failed to set time from GPS: " + std::string(std::strerror(errno)));
        return false;
    }
    aon_timer_start_with_timeofday();
    refreshTimeZoneOffset(gpsUtc);
    return true;
}

bool TimeMgr::refreshTimeZoneOffset(std::time_t whenUtc)
{
    const std::time_t timeToUse = (whenUtc != 0) ? whenUtc : static_cast<std::time_t>(CurrentEpochSeconds());
    if (timeToUse == 0)
    {
        m_hasTimeZoneOffset = false;
        return false;
    }

    float offsetHours = 0.0f;
    bool isDst = false;
    LogInfo("Resolving time zone offset for '" + m_timeZoneName + "' at UTC time " + std::to_string(timeToUse));
    if (!ResolveTimeZoneOffset(m_timeZoneName, timeToUse, offsetHours, &isDst))
    {
        m_hasTimeZoneOffset = false;
        return false;
    }
    LogInfo("Resolved time zone offset: " + std::to_string(offsetHours) + " hours, DST: " + (isDst ? "yes" : "no"));

    m_timeZoneOffsetHours = offsetHours;
    m_isDst = isDst;
    m_hasTimeZoneOffset = true;
    return true;
}

bool TimeMgr::isValid() const
{
    return IsWallClockValid() && m_hasTimeZoneOffset;
}

bool TimeMgr::hasTimeZoneOffset() const
{
    return m_hasTimeZoneOffset;
}

float TimeMgr::timeZoneOffsetHours() const
{
    return m_timeZoneOffsetHours;
}

bool TimeMgr::isDst() const
{
    return m_isDst;
}

const std::string& TimeMgr::timeZoneName() const
{
    return m_timeZoneName;
}

void TimeMgr::setTimeZoneName(std::string timeZoneName)
{
    m_timeZoneName = std::move(timeZoneName);
    m_timeZoneOffsetHours = 0.0f;
    m_isDst = false;
    m_hasTimeZoneOffset = false;
}

DelayedRepeatingTimer::DelayedRepeatingTimer(uint32_t delayMs, uint32_t intervalMs, std::function<void()> callback)
    : m_delayMs(delayMs),
      m_intervalMs(intervalMs),
      m_callback(std::move(callback)),
      m_delayAlarmId(0),
      m_repeatingTimer {},
      m_repeatingActive(false),
      m_running(false)
{
}

DelayedRepeatingTimer::~DelayedRepeatingTimer()
{
    Stop();
}

void DelayedRepeatingTimer::Start()
{
    Stop();

    m_running = true;
    m_delayAlarmId = add_alarm_in_ms(m_delayMs, &DelayedRepeatingTimer::delayAlarmCallback, this, true);
    if (m_delayAlarmId <= 0)
    {
        m_running = false;
    }
}

void DelayedRepeatingTimer::Stop()
{
    if (m_delayAlarmId > 0)
    {
        cancel_alarm(m_delayAlarmId);
        m_delayAlarmId = 0;
    }

    if (m_repeatingActive)
    {
        cancel_repeating_timer(&m_repeatingTimer);
        m_repeatingActive = false;
    }

    m_running = false;
}

bool DelayedRepeatingTimer::IsRunning() const
{
    return m_running;
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

    if (!m_running)
    {
        return 0;
    }

    if (m_callback)
    {
        m_callback();
    }

    if (m_intervalMs == 0)
    {
        m_running = false;
        return 0;
    }

    const int64_t intervalUs = -static_cast<int64_t>(m_intervalMs) * 1000;
    m_repeatingActive = add_repeating_timer_us(intervalUs, &DelayedRepeatingTimer::repeatingTimerCallback, this, &m_repeatingTimer);
    if (!m_repeatingActive)
    {
        m_running = false;
    }

    return 0;
}

bool DelayedRepeatingTimer::onRepeatingTick()
{
    if (!m_running)
    {
        m_repeatingActive = false;
        return false;
    }

    if (m_callback)
    {
        m_callback();
    }

    return m_running;
}

//
// AlarmaTimer implementation
//
AlarmTimer::AlarmTimer(std::function<void()> callback)
    : m_callback(std::move(callback)),
      m_alarmId(0),
      m_running(false)
{
}

AlarmTimer::~AlarmTimer()
{
    Stop();
}

void AlarmTimer::Start(uint32_t delayMs)
{
    Stop();

    m_running = true;
    m_alarmId = add_alarm_in_ms(delayMs, &AlarmTimer::alarmCallback, this, true);
    if (m_alarmId <= 0)
    {
        m_running = false;
    }
}

void AlarmTimer::Stop()
{
    if (m_alarmId > 0)
    {
        cancel_alarm(m_alarmId);
        m_alarmId = 0;
    }
    m_running = false;
}

bool AlarmTimer::IsRunning() const
{
    return m_running;
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
    m_running = false;

    if (m_callback)
    {
        m_callback();
    }

    return 0;
}