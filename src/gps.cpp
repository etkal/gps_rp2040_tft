/*
 * GPS class
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

#include <queue>
#include <iostream>
#include <iomanip>
#include <cmath>

#include "gps.h"
#include "timemgr.h"
#include <pico/sync.h>

typedef enum eSentenceType
{
    kGPGGA,
    kGPGLL,
    kGPGSA,
    kGPGSV,
    kGPRMC,
    kGPVTG,
    kPGTOP,
    kPCD,
} eSentenceType;

static std::map<std::string, eSentenceType> g_SentenceTypeMap = {
    {"$GPGGA", kGPGGA},
    {"$GPGLL", kGPGLL},
    {"$GPGSA", kGPGSA},
    {"$GPGSV", kGPGSV},
    {"$GPRMC", kGPRMC},
    {"$GPVTG", kGPVTG},
    {"$PGTOP", kPGTOP},
    {"$PCD",   kPCD  },
};

static GPS* sg_pGPS = NULL;
static uart_inst_t* sg_pUART = nullptr;

// Static members for RX
volatile char GPS::sm_szBuffer[GPS_BUFSIZE];
volatile size_t GPS::sm_iNext = 0;
std::queue<std::string> GPS::sm_qSentences;

GPS::GPS(uart_inst_t* pUART0, uart_inst_t* pUART1)
    : m_pUART0(pUART0),
      m_pUART1(pUART1),
      m_spSendGpsDataTimer(std::make_unique<AlarmTimer>([this]() {
          m_nLastSendTimerFireMs = to_ms_since_boot(get_absolute_time());
          m_bTuneSendDelayPending = true;
          if (m_nLastGprmcRxMs < m_nLastGpggaRxMs)
          {
              m_bTimerFiredBeforeGprmc = true;
              m_bSendGpsDataDeferredUntilGprmc = true;
              return;
          }
          m_bSendGpsData = true;
      }))
{
}

GPS::~GPS()
{
}

void GPS::SetSentenceCallback(void* pCtx, sentenceCallback pCB)
{
    m_pSentenceCtx = pCtx;
    m_pSentenceCallBack = pCB;
}

void GPS::SetGpsDataCallback(void* pCtx, gpsDataCallback pCB)
{
    m_pGpsDataCtx = pCtx;
    m_pGpsDataCallback = pCB;
}

void GPS::Run()
{
    // Set up GPS
    uart_set_fifo_enabled(m_pUART0, false); // Disable FIFO to get immediate RX interrupts
    sg_pGPS = this;                         // Allow interrupt handler to call us back
    sg_pUART = m_pUART0;
    int uartIRQ = m_pUART0 == uart0 ? UART0_IRQ : UART1_IRQ;
    // Set up and enable the interrupt handler
    irq_set_exclusive_handler(uartIRQ, on_uart_rx);
    irq_set_enabled(uartIRQ, true);
    // Now enable the UART to send interrupts - RX only
    uart_set_irqs_enabled(m_pUART0, true, false);

    std::string strSentence;
    bool bSentAntennaCommands = false;
    while (!m_bExit)
    {
        tight_loop_contents();

        // Read sentence from GPS device
        if (getSentence(strSentence))
        {
            bool bValidSentenceRead = processSentence(strSentence);

            if (nullptr != m_pUART1 && bValidSentenceRead)
            {
                uart_puts(m_pUART1, strSentence.c_str()); // Echo to the listening port
            }

            if (!bSentAntennaCommands && bValidSentenceRead)
            {
                LogInfo("Sending antenna commands\n");
                // Write commands to enable reporting external vs internal antenna.  We wait
                // until some data is received to ensure the GPS has finished initializing.
                std::string strPGCMD("$PGCMD,33,1*6C\r\n"); // Enable antenna output for PA6H
                std::string strCDCMD("$CDCMD,33,1*7C\r\n"); // Enable antenna output for PA1616S
                uart_puts(m_pUART0, strPGCMD.c_str());
                uart_puts(m_pUART0, strCDCMD.c_str());
                bSentAntennaCommands = true;
            }
        }

        if (m_bSendGpsData)
        {
            tuneSendGpsDataDelay();
            m_bSendGpsData = false;
            if (NULL != m_pGpsDataCallback)
            {
                (*m_pGpsDataCallback)(m_pGpsDataCtx, m_spGPSData);
            }
        }
    }
}

bool GPS::processSentence(std::string strSentence)
{
    // Validate the string
    if (!validateSentence(strSentence))
    {
        return false;
    }
    LogInfo("Received: " + strSentence);

    if (NULL != m_pSentenceCallBack)
    {
        (*m_pSentenceCallBack)(m_pSentenceCtx, strSentence);
    }

    if (!m_spGPSData)
    {
        // Guarantee we have an object to update
        m_spGPSData = std::make_shared<GPSData>();
        m_spGPSData->mSatList = m_mSatListPersistent; // restore any previous data
    }

    std::vector<std::string> vElems;
    std::stringstream s_stream(strSentence);
    while (s_stream.good())
    {
        std::string substr;
        getline(s_stream, substr, ','); // get first string delimited by comma
        vElems.push_back(substr);
    }

    if (time_us_64() > m_nSatListTime + 30 * 1000 * 1000) // Nothing in 30 seconds, clear vectors
    {
        if (!m_spGPSData->mSatList.empty())
        {
            LogInfo("Clearing vectors\n");
            m_spGPSData->mSatList.clear();
            m_spGPSData->vUsedList.clear();
        }
    }

    if (vElems.size() == 0)
    {
        LogInfo("No elements found\n");
        return false;
    }

    if (g_SentenceTypeMap.find(vElems[0]) == g_SentenceTypeMap.end())
    {
        return false;
    }

    auto type = g_SentenceTypeMap.at(vElems[0]);

    if (m_bGSVInProgress && type != kGPGSV) // Did not complete
    {
        m_bGSVInProgress = false;
        m_mSatListIncoming.clear();
    }

    switch (type)
    {
    case kGPGGA: // Global Positioning System Fix Data
    {
        m_nLastGpggaRxMs = to_ms_since_boot(get_absolute_time());
        if (m_spSendGpsDataTimer)
        {
            m_spSendGpsDataTimer->Start(m_nSendGpsDataDelayMs);
        }

        if (!vElems[7].empty())
        {
            m_spGPSData->strNumSats = "Sat: " + vElems[7];
        }
        if (!vElems[9].empty())
        {
            double dMeters = std::stod(vElems[9].c_str());
            std::stringstream oss;
            if (dMeters < 1000.0)
            {
                oss << std::fixed << std::setfill(' ') << std::setprecision(1) << dMeters << "m";
            }
            else
            {
                oss << std::setfill(' ') << std::setprecision(0) << dMeters << "m";
            }
            m_spGPSData->strAltitude = oss.str();
        }
        break;
    }
    case kGPGSA: // GPS DOP and active satellites
    {
        m_spGPSData->vUsedList.clear();
        m_spGPSData->strMode3D = vElems[2] + "D";
        if (vElems[2] == "1")
        {
            m_spGPSData->strMode3D = "";
        }
        for (int i = 3; i < 15; ++i)
        {
            if (!vElems[i].empty())
            {
                uint satNum = atoi(vElems[i].c_str());
                if (satNum != 0)
                {
                    m_spGPSData->vUsedList.push_back(satNum);
                }
            }
            else
            {
                break;
            }
        }
        break;
    }
    case kGPGSV: // GPS Satellites in view
    {
        // Multipart, clear any previous data and re-gather
        if (vElems[2] == "1")
        {
            m_mSatListIncoming.clear();
            m_strNumGSV = vElems[1];
            m_bGSVInProgress = true;
        }
        int nNumSatsInGSV = std::min(4, atoi(vElems[3].c_str()) - 4 * (atoi(vElems[2].c_str()) - 1));
        if (m_bGSVInProgress)
        {
            for (int i = 4; i < 4 + 4 * nNumSatsInGSV; i += 4)
            {
                if (!vElems[i].empty() && !vElems[i + 1].empty() && !vElems[i + 2].empty())
                {
                    uint num = atoi(vElems[i].c_str());
                    uint el = atoi(vElems[i + 1].c_str());
                    uint az = atoi(vElems[i + 2].c_str());
                    uint rssi = vElems[i + 3].empty() ? 0 : atoi(vElems[i + 3].c_str());
                    uint rssiScaled = (uint)(std::sqrt((double)rssi / 99.0) * 99.0);
                    m_mSatListIncoming.emplace(std::make_pair(num, SatInfo(num, el, az, rssiScaled)));
                }
            }
            if (vElems[2] == m_strNumGSV) // Last one received
            {
                m_bGSVInProgress = false;
                m_nSatListTime = time_us_64();
                m_spGPSData->mSatList = m_mSatListIncoming;
                m_mSatListPersistent = m_spGPSData->mSatList; // Persist the list
            }
        }
        break;
    }
    case kGPRMC: // Recommended minimum specific GPS/Transit data
    {
        m_nLastGprmcRxMs = to_ms_since_boot(get_absolute_time());
        if (m_bSendGpsDataDeferredUntilGprmc)
        {
            m_bSendGpsDataDeferredUntilGprmc = false;
            if (m_spSendGpsDataTimer)
            {
                // Timer fired before GPRMC for this cycle; send shortly after GPRMC to avoid stale UI updates.
                m_spSendGpsDataTimer->Start(10);
            }
        }
        if (!vElems[1].empty())
        {
            std::string& t = vElems[1];
            m_spGPSData->strGPSTime = t.substr(0, 2) + ":" + t.substr(2, 2) + ":" + t.substr(4, 2) + "Z";
            m_spGPSData->strGPSTimeRaw = t;
        }
        else
        {
            m_spGPSData->strGPSTime = "";
            m_spGPSData->strGPSTimeRaw.clear();
        }

        if (!vElems[9].empty())
        {
            m_spGPSData->strGPSDateRaw = vElems[9];
        }
        else
        {
            m_spGPSData->strGPSDateRaw.clear();
        }

        if (vElems[2] == "A")
        {
            if (!vElems[3].empty() && !vElems[4].empty() && !vElems[5].empty() && !vElems[6].empty())
            {
                m_spGPSData->bHasPosition = true;
                m_spGPSData->strLatitude = convertToDegrees(vElems[3], 7) + vElems[4];
                m_spGPSData->strLongitude = convertToDegrees(vElems[5], 8) + vElems[6];
            }
            if (!vElems[7].empty())
            {
                double dKnots = std::stod(vElems[7].c_str());
                double dMph = dKnots * 1.15078;
                std::stringstream oss;
                if (dMph < 10.0)
                {
                    oss << std::fixed << std::setfill(' ') << std::setprecision(1) << dMph << "mph";
                }
                else
                {
                    oss << std::setfill(' ') << std::setprecision(0) << dMph << "mph";
                }
                m_spGPSData->strSpeed = oss.str();
            }
        }
        else
        {
            m_spGPSData->bHasPosition = false;
        }
        break;
    }
    case kPGTOP: // PA6H External antenna info
    {
        if (vElems[2] == "2")
        {
            m_spGPSData->bExternalAntenna = false;
        }
        if (vElems[2] == "3")
        {
            m_spGPSData->bExternalAntenna = true;
        }
        break;
    }
    case kPCD: // PA1616S External antenna info
    {
        if (vElems[2] == "1")
        {
            m_spGPSData->bExternalAntenna = false;
        }
        if (vElems[2] == "2")
        {
            m_spGPSData->bExternalAntenna = true;
        }
        break;
    }
    default:
        break;
    }
    return true;
}

void GPS::tuneSendGpsDataDelay()
{
    if (!m_bTuneSendDelayPending)
    {
        return;
    }
    m_bTuneSendDelayPending = false;

    constexpr uint32_t kTargetAfterGpggaMs = 700;
    constexpr uint32_t kPreGprmcStepMs = 25;
    constexpr uint32_t kRecoveryStepMs = 1;
    constexpr uint32_t kRecoveryWindowCycles = 20;
    constexpr uint32_t kMinDelayMs = 50;
    constexpr uint32_t kMaxDelayMs = 1200;
#if !defined(NDEBUG)
    static uint32_t s_tuneLogCounter = 0;
    static uint32_t s_deferredCountInWindow = 0;
    static uint32_t s_deferredWindowStartMs = to_ms_since_boot(get_absolute_time());
#endif
    static uint32_t s_noEarlyFireCycles = 0;

#if !defined(NDEBUG)

    const uint32_t nowMs = to_ms_since_boot(get_absolute_time());
    if (nowMs - s_deferredWindowStartMs >= 60000)
    {
        LogInfo("GPS delay tune: deferred_count_last_min=" + std::to_string(s_deferredCountInWindow));
        s_deferredCountInWindow = 0;
        s_deferredWindowStartMs = nowMs;
    }
#endif

    // If the timer fired before this cycle's GPRMC, shift base delay later.
    if (m_bTimerFiredBeforeGprmc)
    {
        m_bTimerFiredBeforeGprmc = false;
        s_noEarlyFireCycles = 0;
#if !defined(NDEBUG)
        ++s_deferredCountInWindow;
#endif
        if (m_nSendGpsDataDelayMs + kPreGprmcStepMs > kMaxDelayMs)
        {
            m_nSendGpsDataDelayMs = kMaxDelayMs;
        }
        else
        {
            m_nSendGpsDataDelayMs += kPreGprmcStepMs;
        }
#if !defined(NDEBUG)
        if ((++s_tuneLogCounter % 10) == 0)
        {
            LogInfo("GPS delay tune: deferred until GPRMC, delay_ms=" + std::to_string(m_nSendGpsDataDelayMs) +
                    ", target_after_gpgga_ms=" + std::to_string(kTargetAfterGpggaMs));
        }
#endif
        return;
    }

    ++s_noEarlyFireCycles;
    int32_t adjustmentMs = 0;
    if (m_nSendGpsDataDelayMs < kTargetAfterGpggaMs)
    {
        adjustmentMs = static_cast<int32_t>(kRecoveryStepMs);
    }
    else if (m_nSendGpsDataDelayMs > kTargetAfterGpggaMs && s_noEarlyFireCycles >= kRecoveryWindowCycles)
    {
        adjustmentMs = -static_cast<int32_t>(kRecoveryStepMs);
        s_noEarlyFireCycles = 0;
    }

    int32_t newDelayMs = static_cast<int32_t>(m_nSendGpsDataDelayMs) + adjustmentMs;
    if (newDelayMs < static_cast<int32_t>(kMinDelayMs))
    {
        newDelayMs = static_cast<int32_t>(kMinDelayMs);
    }
    else if (newDelayMs > static_cast<int32_t>(kMaxDelayMs))
    {
        newDelayMs = static_cast<int32_t>(kMaxDelayMs);
    }
    m_nSendGpsDataDelayMs = static_cast<uint32_t>(newDelayMs);

#if !defined(NDEBUG)
    if ((++s_tuneLogCounter % 10) == 0)
    {
        const uint32_t elapsedAfterGpggaMs = m_nLastSendTimerFireMs - m_nLastGpggaRxMs;
        LogInfo("GPS delay tune: elapsed_after_gpgga_ms=" + std::to_string(elapsedAfterGpggaMs) +
                ", target_after_gpgga_ms=" + std::to_string(kTargetAfterGpggaMs) +
                ", no_early_cycles=" + std::to_string(s_noEarlyFireCycles) + ", delay_ms=" + std::to_string(m_nSendGpsDataDelayMs));
    }
#endif
}

bool GPS::validateSentence(std::string& strSentence)
{
    // Validate format and remove checksum and CRLF
    size_t nLen = strSentence.size();
    if (nLen < 1 || strSentence[0] != '$')
    {
        return false;
    }
    if (nLen < 6 || strSentence.substr(nLen - 2, 2) != "\r\n" || strSentence[nLen - 5] != '*')
    {
        return false;
    }
    std::string specifiedCheck = strSentence.substr(nLen - 4, 2);
    std::string calculatedCheck = checkSum(strSentence.substr(1, nLen - 6));
    if (calculatedCheck != specifiedCheck)
    {
        return false;
    }

    strSentence = strSentence.substr(0, nLen - 5);

    return true;
}

std::string GPS::checkSum(const std::string& strSentence)
{
    uint8_t check = 0;
    for (const char& c : strSentence)
    {
        check ^= (uint8_t)c;
    }
    std::stringstream oss;
    oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (unsigned int)check;
    return oss.str();
}

std::string GPS::convertToDegrees(std::string strRaw, int width)
{
    // Convert (D)DDMM.mmmm to decimal degrees
    double dRawAsDouble = stod(strRaw);
    int firstdigits = int(dRawAsDouble / 100);
    int nexttwodigits = dRawAsDouble - double(firstdigits * 100);
    double converted = double(firstdigits) + nexttwodigits / 60.0;
    std::stringstream oss;
    oss << std::fixed << std::setw(width) << std::setfill(' ') << std::setprecision(4) << converted;
    return oss.str();
}

// RX interrupt handler
void GPS::on_uart_rx()
{
    uart_set_irqs_enabled(sg_pUART, false, false);
    while (uart_is_readable(sg_pUART))
    {
        char ch = uart_getc(sg_pUART);
        sm_szBuffer[sm_iNext] = ch;
        sm_iNext += 1;
        if (sm_iNext >= GPS_BUFSIZE)
        {
            sm_iNext = 0; // wrap around, will sync up eventually
        }
        if (ch == '\n')
        {
            sm_szBuffer[sm_iNext] = '\0';
            sm_qSentences.push(std::string((char*)sm_szBuffer));
            sm_iNext = 0;
        }
    }
    uart_set_irqs_enabled(sg_pUART, true, false);
}

bool GPS::getSentence(std::string& strSentence)
{
    bool bFound = false;
    uart_set_irqs_enabled(sg_pUART, false, false);
    if (!sm_qSentences.empty())
    {
        strSentence = sm_qSentences.front();
        sm_qSentences.pop();
        bFound = true;
    }
    uart_set_irqs_enabled(sg_pUART, true, false);

    return bFound;
}
