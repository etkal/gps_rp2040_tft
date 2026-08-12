/*
 * GPS class
 *
 * (c) 2025-2026 Erik Tkal
 *
 */

#pragma once

#include <pico/stdlib.h>
#include <hardware/uart.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <queue>

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
        : bHasPosition(false),
          bExternalAntenna(false)
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

    bool bHasPosition;
    bool bExternalAntenna;
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

class AlarmTimer;

auto constexpr GPS_BUFSIZE = 256; // Max NMEA-0183 sentence length is actually 82 characters

class GPS
{
public:
    typedef std::shared_ptr<GPS> Shared;

    GPS(uart_inst_t* pUART0, uart_inst_t* pUART1 = nullptr);
    ~GPS();

    void SetSentenceCallback(void* pCtx, sentenceCallback pCB);
    void SetGpsDataCallback(void* pCtx, gpsDataCallback pCB);
    void Run();
    uart_inst_t* GetUART()
    {
        return m_pUART0;
    }

private:
    bool processSentence(std::string strSentence);
    void tuneSendGpsDataDelay();
    bool validateSentence(std::string& strSentence);
    std::string checkSum(const std::string& strSentence);
    std::string convertToDegrees(std::string strRaw, int width);

    uart_inst_t* m_pUART0;
    uart_inst_t* m_pUART1; // output echo

    // RX buffer management
    static volatile char sm_szBuffer[GPS_BUFSIZE];
    static volatile size_t sm_iNext;
    static std::queue<std::string> sm_qSentences;
    static void on_uart_rx();
    static bool getSentence(std::string& strSentence);

    // GPS object members
    bool m_bExit {false};
    bool m_bGSVInProgress {false};
    std::string m_strNumGSV;
    uint64_t m_nSatListTime {0};
    bool m_bSendGpsData {false};
    bool m_bSendGpsDataDeferredUntilGprmc {false};
    bool m_bTimerFiredBeforeGprmc {false};
    uint32_t m_nSendGpsDataDelayMs {700};
    uint32_t m_nLastGpggaRxMs {0};
    uint32_t m_nLastGprmcRxMs {0};
    volatile uint32_t m_nLastSendTimerFireMs {0};
    volatile bool m_bTuneSendDelayPending {false};
    std::unique_ptr<AlarmTimer> m_spSendGpsDataTimer;
    GPSData::Shared m_spGPSData;
    SatList m_mSatListIncoming;
    SatList m_mSatListPersistent;

    sentenceCallback m_pSentenceCallBack {nullptr};
    void* m_pSentenceCtx {nullptr};
    gpsDataCallback m_pGpsDataCallback {nullptr};
    void* m_pGpsDataCtx {nullptr};
};
