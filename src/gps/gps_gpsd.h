/*
 * GPSD client class
 *
 * (c) 2026 Erik Tkal
 *
 */

#pragma once

#include "gps.h"

#include <queue>

#include "lwip/tcp.h"
#include "pico/sync.h"
#include "pico/util/queue.h"

#include "wifi.h"
#include "log.h"

auto constexpr GPS_BUFSIZE = 256;       // Max NMEA-0183 sentence length is actually 82 characters
auto constexpr GPS_QUEUE_SIZE = 16;     // Number of sentences to queue
auto constexpr GPS_RING_BUFSIZE = 4096; // Circular ring buffer size
constexpr uint32_t GPSD_CONNECT_DELAY_INITIAL_MS = 2000;
constexpr uint32_t GPSD_CONNECT_DELAY_STEP_MS = 2000;
constexpr uint32_t GPSD_CONNECT_DELAY_MAX_MS = 10000;

enum class GpsdServerState
{
    DISCONNECTED,
    CONNECTING,
    WAITING_FOR_CONNECT_RESULT,
    CONNECTION_SUCCEEDED,
    CONNECTED,
    ERROR
};

class GPS_gpsd : public GPS
{
public:
    typedef std::shared_ptr<GPS_gpsd> Shared;

    GPS_gpsd();
    ~GPS_gpsd() override;

    void Initialize() override;

private:
    static void wifiStateCallback(void* pCtx, const WifiState state);
    bool serverStateMachine();
    void setServerState(GpsdServerState state)
    {
        auto current = m_gpsdServerState;
        LogInfo("GPSD server state " + gpsdServerStateToString(current) + " -> " + gpsdServerStateToString(state));
        m_gpsdServerState = state;
    }

    bool getSentence(std::string& strSentence) override;
    void closeConnection();
    void writeString(const std::string& str);

    static err_t tcpConnected(void* arg, struct tcp_pcb* pcb, err_t err);
    static err_t tcpPoll(void* arg, struct tcp_pcb* pcb);
    static err_t tcpSent(void* arg, struct tcp_pcb* pcb, u16_t len);
    static err_t tcpRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    static void tcpError(void* arg, err_t err);

    err_t onTcpConnected(struct tcp_pcb* pcb, err_t err);
    err_t onTcpPoll(struct tcp_pcb* pcb);
    err_t onTcpSent(struct tcp_pcb* pcb, u16_t len);
    err_t onTcpRecv(struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    void onTcpError(err_t err);

    // Server state
    static std::string gpsdServerStateToString(GpsdServerState state);
    GpsdServerState m_gpsdServerState {GpsdServerState::DISCONNECTED};

    AlarmTimer::Shared m_spReconnectTimer; // Timer to allow connect attempt
    uint32_t m_connectDelayMs {GPSD_CONNECT_DELAY_INITIAL_MS};
    bool m_bReadyToConnect {false};
    bool m_bWifiConnected {false};
    err_t m_lastTcpError {ERR_OK};

    bool m_bTcpConnectFailed {false};
    bool m_bTcpConnectSucceeded {false};
    bool m_bTcpError {false};

    // TCP RX management
    struct tcp_pcb* m_pTcpPcb {nullptr};
    ip_addr_t m_remoteAddr {};
    absolute_time_t m_wifiConnectTimeout {};

    // RX management (circular ring buffer)
    void processRingBytes(const uint8_t* pBuf, size_t nLen);

    // Ring buffer state
    volatile size_t m_iRingReadPos {0};  // our read offset into the circular buffer
    volatile size_t m_iRingWritePos {0}; // write offset into the circular buffer
    char m_szBuf[GPS_BUFSIZE] {};        // current sentence assembly buffer
    size_t m_iNext {0};                  // write offset into m_szBuf
    // Circular buffer for TCP RX
    char m_szRingBuf[GPS_RING_BUFSIZE] {};

    // Queue for received sentences
    queue_t m_qSentences;
};
