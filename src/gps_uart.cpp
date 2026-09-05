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

#include "gps_uart.h"

#include <queue>
#include <iostream>
#include <iomanip>
#include <cmath>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "timemgr.h"

// Static members for RX
GPS_UART* GPS_UART::sm_pGPS = nullptr;

GPS_UART::GPS_UART()
    : GPS::GPS()
{
}

GPS_UART::~GPS_UART()
{
    // Tear down the DMA channel if it was set up
    if (m_dmaChanRx >= 0)
    {
        dma_channel_abort(m_dmaChanRx);
        dma_channel_unclaim(m_dmaChanRx);
        m_dmaChanRx = -1;
    }
    if (sm_pGPS == this)
    {
        sm_pGPS = nullptr;
    }
    queue_free(&m_qSentences); // Free the queue resources
}

void GPS_UART::SetInputUART(uart_inst_t* pUart,
                            uint tx_gpio,
                            uint rx_gpio,
                            uint data_bits,
                            uint stop_bits,
                            uart_parity_t parity,
                            uint baudrate)
{
    // Set up input UART for GPS device
    m_pUartIn = pUart;
    m_tx_gpio_in = tx_gpio;
    m_rx_gpio_in = rx_gpio;
    m_data_bits_in = data_bits;
    m_stop_bits_in = stop_bits;
    m_parity_in = parity;
    m_baudrate_in = baudrate;
}

void GPS_UART::SetOutputUART(uart_inst_t* pUart,
                             uint tx_gpio,
                             uint rx_gpio,
                             uint data_bits,
                             uint stop_bits,
                             uart_parity_t parity,
                             uint baudrate)
{
    // Set up output UART for echo
    m_pUartOut = pUart;
    m_tx_gpio_out = tx_gpio;
    m_rx_gpio_out = rx_gpio;
    m_data_bits_out = data_bits;
    m_stop_bits_out = stop_bits;
    m_parity_out = parity;
    m_baudrate_out = baudrate;
}

void GPS_UART::Initialize()
{
    GPS::Initialize(); // Call base class Initialize to set up alarm pool and timer

    // Register the callback to handle validated sentences processed by the base class
    sm_pGPS = this;
    GPS::SetSentenceCallback(this, sentenceCB);

    // Initialize the queue for received sentences. This uses the SDK queue_t structure for thread-safe access.
    queue_init(&m_qSentences, GPS_BUFSIZE, GPS_QUEUE_SIZE); // Initialize the queue for received sentences

    if (m_pUartIn == nullptr)
    {
        LogInfo("Input UART not set. Cannot initialize GPS_UART.");
        return;
    }
    uart_init(m_pUartIn, m_baudrate_in);
    gpio_set_function(m_tx_gpio_in, GPIO_FUNC_UART);
    gpio_set_function(m_rx_gpio_in, GPIO_FUNC_UART);
    uart_set_hw_flow(m_pUartIn, false, false);
    uart_set_format(m_pUartIn, m_data_bits_in, m_stop_bits_in, m_parity_in);

    if (m_pUartOut != nullptr)
    {
        uart_init(m_pUartOut, m_baudrate_out);
        gpio_set_function(m_tx_gpio_out, GPIO_FUNC_UART);
        gpio_set_function(m_rx_gpio_out, GPIO_FUNC_UART);
        uart_set_hw_flow(m_pUartOut, false, false);
        uart_set_format(m_pUartOut, m_data_bits_out, m_stop_bits_out, m_parity_out);
    }

    // Set up a circular DMA buffer for UART RX. A single DMA channel continuously streams bytes from
    // the UART data register into m_szDmaBuf in ring mode, wrapping automatically at the end. There is
    // no interrupt: the main loop chases the hardware write pointer (derived from the channel's
    // remaining transfer_count) and consumes bytes at its leisure. This is race-free because the DMA
    // only ever appends and we only ever read up to the last-known write position.
    uart_set_fifo_enabled(m_pUartIn, true); // FIFO must be enabled for UART DMA operation
    uart_set_irqs_enabled(m_pUartIn, false, false);

    m_dmaChanRx = dma_claim_unused_channel(true);
    m_iDmaReadPos = 0;

    dma_channel_config cfgRx = dma_channel_get_default_config(m_dmaChanRx);
    channel_config_set_transfer_data_size(&cfgRx, DMA_SIZE_8);
    channel_config_set_read_increment(&cfgRx, false);
    channel_config_set_write_increment(&cfgRx, true);
    channel_config_set_dreq(&cfgRx, uart_get_dreq_num(m_pUartIn, false));  // pace by UART RX
    channel_config_set_ring(&cfgRx, true, __builtin_ctz(GPS_DMA_BUFSIZE)); // wrap write address at buffer size
    dma_channel_configure(m_dmaChanRx,
                          &cfgRx,
                          m_szDmaBuf,                  // circular destination buffer
                          &uart_get_hw(m_pUartIn)->dr, // UART data register
                          0xFFFFFFFF,                  // effectively never stop; ring mode wraps the address
                          true);                       // start immediately

#if defined(SEND_ANTENNA_STATUS_REQUESTS)
    // Set up a timer to send antenna status commands to the GPS device every 30 seconds, starting after 2 seconds.
    m_spSendAntennaStatusTimer = std::make_shared<DelayedRepeatingTimer>(
        2000,
        30000,
        [this]() {
            sendExternalAntennaStatusRequest();
        },
        m_pAlarmPool);
    m_spSendAntennaStatusTimer->Start();
#endif

    LogInfo("GPS_UART initialization complete.");
}

void GPS_UART::sendExternalAntennaStatusRequest()
{
    LogInfo("Sending antenna status commands to GPS device...");
    // Write commands to enable reporting external vs internal antenna (for PA6H and PA1616S modules).
    std::string strPGCMD("$PGCMD,33,1*6C\r\n"); // Enable antenna output for PA6H
    std::string strCDCMD("$CDCMD,33,1*7C\r\n"); // Enable antenna output for PA1616S
    uart_puts(GetInputUART(), strPGCMD.c_str());
    uart_puts(GetInputUART(), strCDCMD.c_str());
}

// Use this callback from the base class in order to echo sentences received from the
// GPS device to the output UART (if set).
void GPS_UART::sentenceCB(void* pCtx, std::string strSentence)
{
    GPS_UART* pThis = static_cast<GPS_UART*>(pCtx);

    if (nullptr != pThis->m_pUartOut)
    {
        uart_puts(pThis->m_pUartOut, strSentence.c_str()); // Echo to the listening port
    }
}

// Feed received bytes into the sentence assembly buffer. Completed sentences are queued for the main loop.
void GPS_UART::processDmaBytes(const uint8_t* pBuf, size_t nLen)
{
    for (size_t i = 0; i < nLen; ++i)
    {
        char ch = (char)pBuf[i];
        if (ch == '$' && m_iNext != 0)
        {
            // New sentence start with data in the buffer: discard the old partial data
            m_iNext = 0;
        }
        m_szBuf[m_iNext++] = ch;
        if (m_iNext >= GPS_BUFSIZE)
        {
            m_iNext = 0; // overflow, will sync up on next sentence
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

// Get a sentence from the queue. This function will return false if no sentence is available.
bool GPS_UART::getSentence(std::string& strSentence)
{
    // Drain any bytes the DMA has written to the circular buffer since our last pass. The current write
    // position is the channel's write address register relative to the buffer base; in ring mode this
    // always stays within [0, GPS_DMA_BUFSIZE). Read a snapshot before consuming.
    uintptr_t nWriteAddr = dma_channel_hw_addr(m_dmaChanRx)->write_addr;
    uintptr_t nBase = (uintptr_t)m_szDmaBuf;
    size_t nWritePos = (size_t)(nWriteAddr - nBase);
    if (nWritePos >= GPS_DMA_BUFSIZE)
    {
        nWritePos = 0; // defensive clamp (should not happen in ring mode)
    }

    if (nWritePos != m_iDmaReadPos)
    {
        if (nWritePos > m_iDmaReadPos)
        {
            // Contiguous region: read [readPos, writePos)
            processDmaBytes((const uint8_t*)m_szDmaBuf + m_iDmaReadPos, nWritePos - m_iDmaReadPos);
        }
        else
        {
            // Wrapped: read [readPos, end) then [0, writePos)
            processDmaBytes((const uint8_t*)m_szDmaBuf + m_iDmaReadPos, GPS_DMA_BUFSIZE - m_iDmaReadPos);
            processDmaBytes((const uint8_t*)m_szDmaBuf, nWritePos);
        }
        m_iDmaReadPos = nWritePos;
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
