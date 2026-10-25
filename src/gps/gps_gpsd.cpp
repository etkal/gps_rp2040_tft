/*
 * GPSD client class
 *
 * (c) 2026 Erik Tkal
 *
 */

#include "gps_gpsd.h"

#include <algorithm>
#include <iostream>

#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"

#include "wifi.h"
#include "network_info.h"

namespace
{
    constexpr uint8_t pollTimeSeconds = 5;
    constexpr char gpsdWatchCommand[] = "?WATCH={\"nmea\":true}\r\n";
} // namespace

GPS_gpsd::GPS_gpsd()
{
    queue_init(&m_qSentences, GPS_BUFSIZE, GPS_QUEUE_SIZE); // Initialize the queue for received sentences
}

GPS_gpsd::~GPS_gpsd()
{
    closeConnection();
    queue_free(&m_qSentences); // Free the queue resources
}

void GPS_gpsd::Initialize()
{
    GPS::Initialize();

    m_connectDelayMs = GPSD_CONNECT_DELAY_INITIAL_MS;
    m_bReadyToConnect = true;
    WifiConnection::GetInstance()->SetStateCallback(this, wifiStateCallback);

    m_spReconnectTimer = std::make_shared<AlarmTimer>([this]() {
        m_bReadyToConnect = true;
    });
}

void GPS_gpsd::wifiStateCallback(void* pCtx, WifiState state)
{
    GPS_gpsd* pThis = reinterpret_cast<GPS_gpsd*>(pCtx);
    if (state == WifiState::Connected)
    {
        pThis->m_bReadyToConnect = true;
        pThis->m_bWifiConnected = true;
    }
    else
    {
        pThis->m_bWifiConnected = false;
    }
}

bool GPS_gpsd::serverStateMachine()
{
    if (!m_bWifiConnected && GpsdServerState::ERROR != m_gpsdServerState && GpsdServerState::DISCONNECTED != m_gpsdServerState)
    {
        setServerState(GpsdServerState::ERROR);
        return false;
    }

    err_t err = ERR_OK;
    uint32_t connDelayMs;
    switch (m_gpsdServerState)
    {
    case GpsdServerState::DISCONNECTED:
        if (!m_bReadyToConnect || !m_bWifiConnected)
        {
            return false;
        }
        m_bReadyToConnect = false; // reset the flag as we are about to attempt a connection

        ip4addr_aton(g_szGpsdIpAddress, &m_remoteAddr);
        LogInfo("Preparing connection to " + std::string(ip4addr_ntoa(&m_remoteAddr)) + " port " + std::to_string(g_nGpsdTcpPort));
        m_pTcpPcb = tcp_new_ip_type(IP_GET_TYPE(m_remoteAddr));
        if (m_pTcpPcb == nullptr)
        {
            LogInfo("Unable to allocate TCP control block for gpsd");
            panic("Unable to allocate TCP control block for gpsd");
            return false;
        }
        tcp_arg(m_pTcpPcb, this);
        tcp_poll(m_pTcpPcb, tcpPoll, pollTimeSeconds * 2);
        tcp_sent(m_pTcpPcb, tcpSent);
        tcp_recv(m_pTcpPcb, tcpRecv);
        tcp_err(m_pTcpPcb, tcpError);
        tcp_nagle_disable(m_pTcpPcb);
        setServerState(GpsdServerState::CONNECTING);
        return false;

    case GpsdServerState::CONNECTING:
        LogInfo("Attempting to connect to gpsd server");
        if (m_pMessageCallback)
        {
            m_pMessageCallback(m_pMessageCtx, "Connecting to server");
        }
        cyw43_arch_lwip_begin();
        err = tcp_connect(m_pTcpPcb, &m_remoteAddr, g_nGpsdTcpPort, tcpConnected);
        cyw43_arch_lwip_end();
        if (err != ERR_OK)
        {
            LogInfo("Error connecting to gpsd: " + std::to_string(err));
            LogInfo("Setting GPSD server state to ERROR");
            setServerState(GpsdServerState::ERROR);
            return false;
        }
        LogInfo("Waiting for tcp_connect result");
        setServerState(GpsdServerState::WAITING_FOR_CONNECT_RESULT);
        return false;

    case GpsdServerState::WAITING_FOR_CONNECT_RESULT:
        if (m_bTcpConnectFailed || m_bTcpError)
        {
            LogInfo("GPSD server connection failed with TCP error " + std::to_string(m_lastTcpError) + ".");
            m_bTcpConnectFailed = false;
            m_bTcpError = false;
            m_lastTcpError = ERR_OK;
            setServerState(GpsdServerState::ERROR);
        }
        if (m_bTcpConnectSucceeded)
        {
            LogInfo("GPSD server connection established successfully.");
            m_bTcpConnectSucceeded = false;
            setServerState(GpsdServerState::CONNECTION_SUCCEEDED);
        }
        return false;

    case GpsdServerState::CONNECTION_SUCCEEDED:
        LogInfo("Sending watch command: " + std::string(gpsdWatchCommand));
        writeString(gpsdWatchCommand);
        setServerState(GpsdServerState::CONNECTED);
        m_connectDelayMs = GPSD_CONNECT_DELAY_INITIAL_MS;
        return false;

    case GpsdServerState::CONNECTED:
        if (m_bTcpError)
        {
            m_bTcpError = false;
            setServerState(GpsdServerState::ERROR);
            return false;
        }
        return true;

    case GpsdServerState::ERROR:
        LogInfo("GPSD server connection error");
        closeConnection();
        setServerState(GpsdServerState::DISCONNECTED);

        // Schedule a reconnect attempt
        connDelayMs = m_connectDelayMs;
        m_connectDelayMs = std::min(m_connectDelayMs + GPSD_CONNECT_DELAY_STEP_MS, GPSD_CONNECT_DELAY_MAX_MS);
        m_spReconnectTimer->Start(connDelayMs);
        return false;

    default:
        return false;
    }
    return false;
}

// Get a sentence from the queue. This function will return false if no sentence is available.
bool GPS_gpsd::getSentence(std::string& strSentence)
{
    if (!serverStateMachine())
    {
        return false;
    }

    // Drain any bytes written to the circular buffer since our last pass.
    size_t nWritePos = m_iRingWritePos;
    size_t nReadPos = m_iRingReadPos;

    if (nWritePos != nReadPos)
    {
        if (nWritePos > nReadPos)
        {
            // Contiguous region: read [readPos, writePos)
            processRingBytes(reinterpret_cast<const uint8_t*>(m_szRingBuf) + nReadPos, nWritePos - nReadPos);
        }
        else
        {
            // Wrapped: read [readPos, end) then [0, writePos)
            processRingBytes(reinterpret_cast<const uint8_t*>(m_szRingBuf) + nReadPos, GPS_RING_BUFSIZE - nReadPos);
            processRingBytes(reinterpret_cast<const uint8_t*>(m_szRingBuf), nWritePos);
        }
        m_iRingReadPos = nWritePos;
    }

    bool bFound = false;

    // Check if there are any sentences in the queue. If so, remove one and return it.
    if (queue_try_peek(&m_qSentences, nullptr))
    {
        char szBuffer[GPS_BUFSIZE];
        if (queue_try_remove(&m_qSentences, szBuffer))
        {
            strSentence = std::string(szBuffer);
            bFound = true;
        }
    }
    return bFound;
}

void GPS_gpsd::closeConnection()
{
    if (m_pTcpPcb == nullptr)
    {
        return;
    }

    tcp_arg(m_pTcpPcb, nullptr);
    tcp_poll(m_pTcpPcb, nullptr, 0);
    tcp_sent(m_pTcpPcb, nullptr);
    tcp_recv(m_pTcpPcb, nullptr);
    tcp_err(m_pTcpPcb, nullptr);
    if (tcp_close(m_pTcpPcb) != ERR_OK)
    {
        tcp_abort(m_pTcpPcb);
    }
    m_pTcpPcb = nullptr;
}

void GPS_gpsd::writeString(const std::string& str)
{
    cyw43_arch_lwip_begin();
    err_t err = tcp_write(m_pTcpPcb, str.c_str(), str.length(), TCP_WRITE_FLAG_COPY);
    cyw43_arch_lwip_end();
    if (err != ERR_OK)
    {
        std::cout << "lwIP tcp_write error " << std::to_string(err) << std::endl;
    }
}

err_t GPS_gpsd::tcpConnected(void* arg, struct tcp_pcb* pcb, err_t err)
{
    return static_cast<GPS_gpsd*>(arg)->onTcpConnected(pcb, err);
}

err_t GPS_gpsd::tcpPoll(void* arg, struct tcp_pcb* pcb)
{
    return static_cast<GPS_gpsd*>(arg)->onTcpPoll(pcb);
}

err_t GPS_gpsd::tcpSent(void* arg, struct tcp_pcb* pcb, u16_t len)
{
    return static_cast<GPS_gpsd*>(arg)->onTcpSent(pcb, len);
}

err_t GPS_gpsd::tcpRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err)
{
    return static_cast<GPS_gpsd*>(arg)->onTcpRecv(pcb, p, err);
}

void GPS_gpsd::tcpError(void* arg, err_t err)
{
    static_cast<GPS_gpsd*>(arg)->onTcpError(err);
}

err_t GPS_gpsd::onTcpConnected([[maybe_unused]] struct tcp_pcb* pcb, err_t err)
{
    if (err != ERR_OK)
    {
        // Unsuccessful, set error in order to retry
        m_bTcpConnectFailed = true;
        m_lastTcpError = err;
        return ERR_OK;
    }
    m_bTcpConnectSucceeded = true;
    return ERR_OK;
}

err_t GPS_gpsd::onTcpPoll([[maybe_unused]] struct tcp_pcb* pcb)
{
    return ERR_OK;
}

err_t GPS_gpsd::onTcpSent([[maybe_unused]] struct tcp_pcb* pcb, [[maybe_unused]] u16_t len)
{
    return ERR_OK;
}

err_t GPS_gpsd::onTcpRecv(struct tcp_pcb* pcb, struct pbuf* p, [[maybe_unused]] err_t err)
{
    cyw43_arch_lwip_check();
    if (p == nullptr)
    {
        LogInfo("Received null pbuf.");
        m_bTcpError = true;
        return ERR_OK;
    }

    for (pbuf* buffer = p; buffer != nullptr; buffer = buffer->next)
    {
        const char* payload = static_cast<const char*>(buffer->payload);
        for (uint16_t index = 0; index < buffer->len; ++index)
        {
            m_szRingBuf[m_iRingWritePos] = payload[index];
            m_iRingWritePos = (m_iRingWritePos + 1) % GPS_RING_BUFSIZE;
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void GPS_gpsd::onTcpError(err_t err)
{
    m_pTcpPcb = nullptr;
    m_bTcpError = true;
    m_lastTcpError = err;
}

// Feed received bytes into the sentence assembly buffer. Completed sentences are queued for the main loop.
void GPS_gpsd::processRingBytes(const uint8_t* pBuf, size_t nLen)
{
    for (size_t i = 0; i < nLen; ++i)
    {
        char ch = static_cast<char>(pBuf[i]);
        if (ch == '$')
        {
            // Start of a new NMEA sentence
            m_iNext = 0;
            m_szBuf[m_iNext++] = ch;
        }
        else if (m_iNext > 0)
        {
            if (m_iNext < GPS_BUFSIZE - 1)
            {
                m_szBuf[m_iNext++] = ch;
            }
            else
            {
                // Overflow without newline; reset buffer
                m_iNext = 0;
            }

            if (ch == '\n')
            {
                m_szBuf[m_iNext] = '\0';
                if (!queue_try_add(&m_qSentences, m_szBuf))
                {
                    // Should never happen if the queue is sized appropriately
                    printf("Queue full\n");
                }
                m_iNext = 0;
            }
        }
    }
}

std::string GPS_gpsd::gpsdServerStateToString(GpsdServerState state)
{
    switch (state)
    {
    case GpsdServerState::DISCONNECTED:
        return "DISCONNECTED";
    case GpsdServerState::CONNECTING:
        return "CONNECTING";
    case GpsdServerState::WAITING_FOR_CONNECT_RESULT:
        return "WAITING_FOR_CONNECT_RESULT";
    case GpsdServerState::CONNECTION_SUCCEEDED:
        return "CONNECTION_SUCCEEDED";
    case GpsdServerState::CONNECTED:
        return "CONNECTED";
    case GpsdServerState::ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
