/*
 * GPS class
 *
 * (c) 2025-2026 Erik Tkal
 *
 */

#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <queue>

#include "pico/sync.h"

#include "timemgr.h"

class SatInfo
{
public:
    SatInfo(uint num = 0, uint el = 0, uint az = 0, uint rssi = 0)
    {
        m_num = num;
        m_el = el;
        m_az = az;
        m_rssi = rssi;
    }
    ~SatInfo()
    {
    }

    uint m_num;
    uint m_el;
    uint m_az;
    uint m_rssi;
};

typedef std::map<uint, SatInfo> SatList;
typedef std::vector<uint> UsedList;

class GPSData
{
public:
    typedef std::shared_ptr<GPSData> Shared;

    GPSData()
    {
    }

    GPSData(const GPSData& rhs)
        : bHasPosition(rhs.bHasPosition),
          bExternalAntenna(rhs.bExternalAntenna),
          strLatitude(rhs.strLatitude),
          strLongitude(rhs.strLongitude),
          strAltitude(rhs.strAltitude),
          strNumSats(rhs.strNumSats),
          strGPSTimeRaw(rhs.strGPSTimeRaw),
          strGPSDateRaw(rhs.strGPSDateRaw),
          strGPSTime(rhs.strGPSTime),
          strMode3D(rhs.strMode3D),
          strSpeed(rhs.strSpeed),
          mSatList(rhs.mSatList),
          vUsedList(rhs.vUsedList)
    {
    }

    ~GPSData() = default;

    bool bHasPosition {false};
    bool bExternalAntenna {false};
    std::string strLatitude;
    std::string strLongitude;
    std::string strAltitude;
    std::string strNumSats;
    std::string strGPSTimeRaw; // Raw GPS time in HHMMSS format
    std::string strGPSDateRaw; // Raw GPS date in DDMMYY format
    std::string strGPSTime;    // Formatted GPS time string in HH:MM:SSZ format
    std::string strMode3D;
    std::string strSpeed;
    SatList mSatList;
    UsedList vUsedList;
};

typedef void (*sentenceCallback)(void* pCtx, std::string strSentence);
typedef void (*gpsDataCallback)(void* pCtx, GPSData::Shared spGPSData);

class GPS
{
public:
    typedef std::shared_ptr<GPS> Shared;

    GPS();
    virtual ~GPS();

    // Derived classes can perform their own initialization and also invoke the base class function
    virtual void Initialize();
    void Run();
    void RunOnce();
    void Stop();

    void SetSentenceCallback(void* pCtx, sentenceCallback pCB);
    void SetGpsDataCallback(void* pCtx, gpsDataCallback pCB);

protected:
    alarm_pool_t* m_pAlarmPool {nullptr};

    // Derived classes must implement this to retrieve sentences safely, e.g. to handle IRQ enable/disable
    // or by using a critical section.
    virtual bool getSentence(std::string& strSentence) = 0;

private:
    bool processSentence(std::string strSentence);
    bool validateSentence(std::string& strSentence);
    std::string checkSum(const std::string& strSentence);
    std::string convertToDegrees(std::string strRaw, int width);

    // GPS object members
    bool m_bExit {false};
    bool m_bGSVInProgress {false};
    std::string m_strNumGSV;
    uint64_t m_nSatListTime {0};
    bool m_bSendGpsData {false};
    GPSData::Shared m_spGPSData;
    SatList m_mSatListIncoming;
    SatList m_mSatListPersistent;

    AlarmTimer::Shared m_spSendDataTimer; // Delay after receiving a specific sentence before sending GPS data
    AlarmTimer::Shared m_spIdleTimer;     // Timer to detect lack of GPS data
    sentenceCallback m_pSentenceCallBack {nullptr};
    void* m_pSentenceCtx {nullptr};
    gpsDataCallback m_pGpsDataCallback {nullptr};
    void* m_pGpsDataCtx {nullptr};
};

// Helper class for safe GPS sentence processing.
// This class encapsulates a GPS sentence, allowing for safe handling of sentence elements.
class GPSSentence
{
public:
    GPSSentence(std::string strSentence)
        : m_strSentence(std::move(strSentence))
    {
        std::stringstream s_stream(m_strSentence);
        while (s_stream.good())
        {
            std::string substr;
            getline(s_stream, substr, ','); // get string delimited by comma
            m_vElems.push_back(substr);
        }
    }

    std::string operator[](size_t index) const
    {
        if (index < m_vElems.size())
        {
            return m_vElems[index];
        }
        return "";
    }

    bool operator()() const
    {
        return !m_vElems.empty();
    }

    ~GPSSentence() = default;

    const std::string& getSentence() const
    {
        return m_strSentence;
    }

private:
    std::string m_strSentence;
    std::vector<std::string> m_vElems;
};
