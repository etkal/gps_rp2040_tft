/*
 * GPS_UART class
 *
 * (c) 2026 Erik Tkal
 *
 * Derived class of GPS that implements UART interface for receiving NMEA-0183 sentences from a GPS device.
 *
 */

#pragma once

#include "gps.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/util/queue.h"

#include "timemgr.h"

auto constexpr GPS_BUFSIZE = 96;      // Max NMEA-0183 sentence length is actually 82 characters
auto constexpr GPS_QUEUE_SIZE = 16;   // Number of sentences to queue
auto constexpr GPS_DMA_BUFSIZE = 256; // Circular DMA buffer size; must be a power of two for ring mode

class GPS_UART : public GPS
{
public:
    typedef std::shared_ptr<GPS_UART> Shared;

    GPS_UART();
    ~GPS_UART();

    void SetInputUART(uart_inst_t* pUart, uint tx_gpio, uint rx_gpio, uint data_bits, uint stop_bits, uart_parity_t parity, uint baudrate);
    void SetOutputUART(uart_inst_t* pUart, uint tx_gpio, uint rx_gpio, uint data_bits, uint stop_bits, uart_parity_t parity, uint baudrate);
    void Initialize() override;

    inline uart_inst_t* GetInputUART()
    {
        return m_pUartIn;
    }

private:
    virtual bool getSentence(std::string& strSentence) override;

    // Send commands to report external antenna status.
#if defined(SEND_ANTENNA_STATUS_REQUESTS)
    void sendExternalAntennaStatusRequest();
#endif

    // Callback for valid sentence received (for output echo)
    static void sentenceCB(void* pCtx, std::string strSentence);

    uart_inst_t* m_pUartIn {nullptr}; // input from GPS device
    uint m_tx_gpio_in {0};
    uint m_rx_gpio_in {0};
    uint m_data_bits_in {0};
    uint m_stop_bits_in {0};
    uart_parity_t m_parity_in {UART_PARITY_NONE};
    uint m_baudrate_in {0};
    uart_inst_t* m_pUartOut {nullptr}; // output echo
    uint m_tx_gpio_out {0};
    uint m_rx_gpio_out {0};
    uint m_data_bits_out {0};
    uint m_stop_bits_out {0};
    uart_parity_t m_parity_out {UART_PARITY_NONE};
    uint m_baudrate_out {0};

#if defined(SEND_ANTENNA_STATUS_REQUESTS)
    DelayedRepeatingTimer::Shared m_spSendAntennaStatusTimer;
#endif

    // RX management (circular DMA buffer)
    static GPS_UART* sm_pGPS; // retained for potential static callbacks
    void processDmaBytes(const uint8_t* pBuf, size_t nLen);

    // DMA ring state. A single channel streams UART RX bytes into m_szDmaBuf in ring mode; the
    // main loop chases the hardware write pointer (derived from the channel's transfer_count).
    int m_dmaChanRx {-1};      // DMA channel: UART RX -> circular buffer
    size_t m_iDmaReadPos {0};  // our read offset into the circular buffer
    char m_szBuf[GPS_BUFSIZE]; // current sentence assembly buffer
    size_t m_iNext {0};        // write offset into m_szBuf
    // DMA circular buffer for UART RX
    char m_szDmaBuf[GPS_DMA_BUFSIZE] __attribute__((aligned(GPS_DMA_BUFSIZE))); // ring buffer, aligned to its size

    // Queue for received sentences
    queue_t m_qSentences;
};
