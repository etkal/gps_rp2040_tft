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

#include "gps.h"

#include <iostream>
#include <iomanip>
#include <cmath>

#include "timemgr.h"

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

namespace
{
    constexpr uint32_t gpsDataPublishDelayMs = 0;
}

GPS::GPS()
{
}

GPS::~GPS()
{
    if (m_spSendDataTimer)
    {
        m_spSendDataTimer->Stop();
        m_spSendDataTimer.reset();
    }
    if (m_spIdleTimer)
    {
        m_spIdleTimer->Stop();
        m_spIdleTimer.reset();
    }
    if (m_pAlarmPool)
    {
        if (1 == get_core_num())
        {
            LogInfo("GPS::~GPS() - Deleting alarm pool for core 1");
            alarm_pool_destroy(m_pAlarmPool);
        }
    }
}

// Set the callback for when a valid sentence is received. This can be used by a derived class
// to echo the sentence to another UART.
void GPS::SetSentenceCallback(void* pCtx, sentenceCallback pCB)
{
    m_pSentenceCtx = pCtx;
    m_pSentenceCallBack = pCB;
}

// Set the callback for when new GPS data is desired to be sent, e.g. for display.
void GPS::SetGpsDataCallback(void* pCtx, gpsDataCallback pCB)
{
    m_pGpsDataCtx = pCtx;
    m_pGpsDataCallback = pCB;
}

void GPS::Initialize()
{
    // If we are on core 1 we need to ensure timers fire on that core.
    if (1 == get_core_num())
    {
        LogInfo("GPS::Initialize() - Creating alarm pool for core 1");
        m_pAlarmPool = alarm_pool_create(1, 16);
    }
    else
    {
        LogInfo("GPS::Initialize() - Using alarm pool for default core");
        m_pAlarmPool = alarm_pool_get_default();
    }

    // Create the timer for sending GPS data to the callback. This is not strictly necessary,
    // but it allows us to wait for the rest of the sentences to arrive, e.g. GPGSV, before sending
    // the data to the callback. Without a delay the display will update more quickly with the
    // time information, but the satellite list may be stale, though that is not critical.
    m_spSendDataTimer = std::make_shared<AlarmTimer>(
        [this]() {
            if (NULL != m_pGpsDataCallback)
            {
                (*m_pGpsDataCallback)(m_pGpsDataCtx, m_spGPSData);
            }
        },
        m_pAlarmPool);

    // Create the idle timer to detect lack of GPS data. If no data is received for a period of time,
    // we will clear the GPS data object so as to invalidate position information, etc.
    m_spIdleTimer = std::make_shared<AlarmTimer>(
        [this]() {
            LogInfo("GPS - No GPS data received, clearing GPS data");
            m_spGPSData.reset();
        },
        m_pAlarmPool);
}

// Main loop for processing GPS sentences. This function will run until Stop() is called.
void GPS::Run()
{
    std::string strSentence;
    while (!m_bExit)
    {
        // Read sentence from GPS device
        if (getSentence(strSentence))
        {
            m_spIdleTimer->Start(5000);   // Start the idle timer to 5 seconds
            processSentence(strSentence); // Process the sentence and update GPS data
        }

        if (m_bSendGpsData)
        {
            m_bSendGpsData = false;
            m_spSendDataTimer->Start(gpsDataPublishDelayMs);
        }
    }
}

// Stop the GPS processing loop. This will cause Run() to return.
void GPS::Stop()
{
    m_bExit = true;
}

// Handle a received sentence. This function will validate the sentence and update the GPS data accordingly.
bool GPS::processSentence(std::string strSentence)
{
    // Validate the string
    if (!validateSentence(strSentence))
    {
        return false;
    }

    LogInfo("Received: " + strSentence); // Log the full received sentence for debugging purposes

    // Call the sentence callback if set
    if (NULL != m_pSentenceCallBack)
    {
        (*m_pSentenceCallBack)(m_pSentenceCtx, strSentence + "\r\n");
    }

    if (!m_spGPSData)
    {
        // Guarantee we have an object to update. This object persist across sentences so as to
        // maintain the satellite list and other data that may not be present in every sentence.
        m_spGPSData = std::make_shared<GPSData>();
        m_spGPSData->mSatList = m_mSatListPersistent; // restore any previous data
    }

    // At this point we have a valid sentence, so we can parse it and update the GPS data object.
    GPSSentence vElems(strSentence.substr(0, strSentence.find('*'))); // Exclude the checksum for parsing

    if (time_us_64() > m_nSatListTime + 30 * 1000 * 1000) // Nothing in 30 seconds, clear vectors
    {
        if (!m_spGPSData->mSatList.empty())
        {
            LogInfo("Clearing vectors\n");
            m_spGPSData->mSatList.clear();
            m_spGPSData->vUsedList.clear();
        }
    }

    if (vElems[0].empty())
    {
        LogInfo("No elements found\n");
        return false;
    }

    if (g_SentenceTypeMap.find(vElems[0]) == g_SentenceTypeMap.end())
    {
        // LogInfo("Unknown sentence type: " + vElems[0]);
        return true; // Not an error, just not handled
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
        // Check for updated time. If different then this is the first time-containing sentence
        // and we should schedule an update of the GPS data and send it to the callback so as that
        // the UI clock be as correct as possible, within reason. The GPRMC handler has this same
        // logic in case (as with some GPS modules) that one comes first.
        if (!vElems[1].empty() && vElems[1].length() >= 6)
        {
            std::string t = vElems[1];
            if (t != m_spGPSData->strGPSTimeRaw)
            {
                // The time value has changed.
                m_bSendGpsData = true;
            }
            m_spGPSData->strGPSTime = t.substr(0, 2) + ":" + t.substr(2, 2) + ":" + t.substr(4, 2) + "Z";
            m_spGPSData->strGPSTimeRaw = t;
        }
        else
        {
            m_spGPSData->strGPSTime = "";
            m_spGPSData->strGPSTimeRaw.clear();
        }
        if (!vElems[7].empty())
        {
            m_spGPSData->strNumSats = "Sat: " + vElems[7];
        }
        else
        {
            m_spGPSData->strNumSats = "";
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
        else
        {
            m_spGPSData->strAltitude = "";
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
        // See the GPGGA case for the same logic regarding time. This is here in case the GPRMC sentence comes first.
        if (!vElems[1].empty() && vElems[1].length() >= 6)
        {
            std::string t = vElems[1];
            if (t != m_spGPSData->strGPSTimeRaw)
            {
                m_bSendGpsData = true;
            }
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
            m_spGPSData->strLatitude.clear();
            m_spGPSData->strLongitude.clear();
            m_spGPSData->strSpeed.clear();
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

bool GPS::validateSentence(std::string& strSentence)
{
    // Validate format and remove CRLF
    bool bValid = true;
    std::string strReason;
    size_t nLen = strSentence.size();
    if (nLen < 1 || strSentence[0] != '$')
    {
        strReason = " (no $)";
        bValid = false;
    }
    else if (nLen < 6 || strSentence.substr(nLen - 2, 2) != "\r\n" || strSentence[nLen - 5] != '*')
    {
        strReason = " (no CRLF or *)";
        bValid = false;
    }
    else
    {
        std::string specifiedCheck = strSentence.substr(nLen - 4, 2);
        std::string calculatedCheck = checkSum(strSentence.substr(1, nLen - 6));
        if (calculatedCheck != specifiedCheck)
        {
            strReason = " (checksum mismatch - rcvd: " + specifiedCheck + ", calc: " + calculatedCheck + ")";
            bValid = false;
        }
    }

    // Strip any CR/LF
    if (nLen >= 2 && strSentence[nLen - 2] == '\r' && strSentence[nLen - 1] == '\n')
    {
        strSentence = strSentence.substr(0, nLen - 2);
    }
    else if (nLen >= 1 && (strSentence[nLen - 1] == '\r' || strSentence[nLen - 1] == '\n'))
    {
        strSentence = strSentence.substr(0, nLen - 1);
    }
    if (!bValid)
    {
        LogInfo("Validation failure: " + strSentence + strReason);
        strSentence.clear();
    }

    return bValid;
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
