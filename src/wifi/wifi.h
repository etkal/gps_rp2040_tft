/*
 * Wifi connection state machine (station mode only).
 *
 * Copyright (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "pico/stdlib.h"

enum class WifiState
{
    Disabled,
    Disconnected,
    Connecting,
    Connected,
    Error,
    Unknown
};

// WifiConnection drives a non-blocking cyw43 station-mode connection state machine.
// Call DoWork() frequently from the main loop; IsConnected() reports link status.
class WifiConnection
{
public:
    typedef std::shared_ptr<WifiConnection> Shared;
            typedef void (*wifiStateCallback)(void* pCtx, WifiState state);

    WifiConnection();
    ~WifiConnection() = default;

    static const Shared& GetInstance();

    void Initialize(std::string ssid, std::string password);
    static void InitializeSingleton(std::string ssid, std::string password);

    void Disable();
    void Enable();
    void SetStateCallback(void* pCtx, wifiStateCallback callback);
    void DoWork();

    bool IsConnected() const
    {
        return m_state == WifiState::Connected;
    }
    WifiState GetState() const
    {
        return m_state;
    }

    static std::string StateToString(WifiState state);

private:
    void setState(WifiState state);
    void scheduleRetry();

    static WifiConnection::Shared sm_spWifiConnection;

    bool m_bInitialized {false};
    bool m_bDisabling {false};
    std::string m_ssid;
    std::string m_password;
    WifiState m_state {WifiState::Disabled};
    wifiStateCallback m_stateCallback {nullptr};
    void* m_pStateCbCtx {nullptr};

    absolute_time_t m_connectTimeout {};
    absolute_time_t m_nextConnectAttempt {};
    uint32_t m_retryDelayMs {0};
};
