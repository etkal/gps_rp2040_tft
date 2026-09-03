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

#include "timemgr.h"

// Static members for RX
GPS_UART* GPS_UART::sm_pGPS = nullptr;
char GPS_UART::sm_szBuffer[GPS_BUFSIZE];
size_t GPS_UART::sm_iNext = 0;

GPS_UART::GPS_UART()
    : GPS::GPS()
{
}

GPS_UART::~GPS_UART()
{
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

    uart_set_fifo_enabled(m_pUartIn, false); // Disable FIFO to get immediate RX interrupts
    int uartIRQ = (uart0 == sm_pGPS->GetInputUART() ? UART0_IRQ : UART1_IRQ);
    // Set up and enable the interrupt handler
    irq_set_exclusive_handler(uartIRQ, on_uart_rx);
    irq_set_enabled(uartIRQ, true);
    // Now enable the UART to send interrupts - RX only
    uart_set_irqs_enabled(m_pUartIn, true, false);

#if defined(SEND_ANTENNA_STATUS_REQUESTS)
    // Set up a timer to send antenna status commands to the GPS device every 30 seconds, starting after 2 seconds.
    // We only use this to set a flag in the GPS_UART class, which is then used as sentences are received to attempt
    // to send the antenna status commands. This is done to avoid sending the commands while the GPS device is busy
    // sending sentences, which can cause the GPS device behave badly.
    m_spSendAntennaStatusTimer = std::make_shared<DelayedRepeatingTimer>(
        2000,
        30000,
        [this]() {
            m_bSendAntennaStatus = true;
        },
        m_pAlarmPool);
    m_spSendAntennaStatusTimer->Start();
    // Alarm timer to delay the sending of those sences.
    m_spSendAntennaStatusAlarm = std::make_shared<AlarmTimer>(
        [this]() {
            sendExternalAntennaStatusRequest();
        },
        m_pAlarmPool);
#endif

    LogInfo("GPS_UART initialization complete.");
}

#if defined(SEND_ANTENNA_STATUS_REQUESTS)
void GPS_UART::sendExternalAntennaStatusRequest()
{
    LogInfo("Sending antenna status commands to GPS device...");
    // Write commands to enable reporting external vs internal antenna (for PA6H and PA1616S modules).
    std::string strPGCMD("$PGCMD,33,1*6C\r\n"); // Enable antenna output for PA6H
    std::string strCDCMD("$CDCMD,33,1*7C\r\n"); // Enable antenna output for PA1616S
    uart_puts(GetInputUART(), strPGCMD.c_str());
    uart_puts(GetInputUART(), strCDCMD.c_str());
}
#endif

// Use this callback from the base class in order to echo sentences received from the
// GPS device to the output UART (if set).
void GPS_UART::sentenceCB(void* pCtx, std::string strSentence)
{
    GPS_UART* pThis = static_cast<GPS_UART*>(pCtx);

#if defined(SEND_ANTENNA_STATUS_REQUESTS)
    static uint64_t nLastSentenceTime = 0;
    uint64_t nNow = time_us_64();
    if (pThis->m_bSendAntennaStatus && nNow - nLastSentenceTime > 250000)
    {
        // We've detected a gap, assume this is the first sentence in a group, so delay the sending
        // of the antenna status commands for 500 ms to attempt to send it after this group.
        LogInfo("GPS_UART - Detected sentence gap, delaying antenna status commands");
        pThis->m_spSendAntennaStatusAlarm->Start(500);
        pThis->m_bSendAntennaStatus = false;
    }
    nLastSentenceTime = nNow;
#endif

    if (nullptr != pThis->m_pUartOut)
    {
        uart_puts(pThis->m_pUartOut, strSentence.c_str()); // Echo to the listening port
    }
}

// RX interrupt handler. This function is called when data is received on the input UART. It reads characters from the UART and assembles
// them into sentences, which are then added to a queue for processing. The SDK queue_t structure is used for thread-safe access to the
// queue from both the interrupt handler and the main processing loop.
void GPS_UART::on_uart_rx()
{
    uart_inst_t* pUart = sm_pGPS->GetInputUART();
    while (uart_is_readable(pUart))
    {
        char ch = uart_getc(pUart);
        if (ch == '$' && sm_iNext != 0)
        {
            // If we see a new sentence start and we have data in the buffer, discard the old data
            sm_iNext = 0;
        }
        sm_szBuffer[sm_iNext] = ch;
        sm_iNext += 1;
        if (sm_iNext >= GPS_BUFSIZE)
        {
            sm_iNext = 0; // wrap around, will sync up eventually
        }
        if (ch == '\n')
        {
            sm_szBuffer[sm_iNext] = '\0';
            if (!queue_try_add(&sm_pGPS->m_qSentences, sm_szBuffer))
            {
                // Should never happen if the queue is sized appropriately. Using the queue_get_max_level()
                // function (if so compiled) shows the queue never exceeded 1 in testing, so a queue size
                // of 16 is more than sufficient.
                printf("Queue full\n");
            }
            sm_iNext = 0;
        }
    }
}

// Get a sentence from the queue. This function will return false if no sentence is available.
bool GPS_UART::getSentence(std::string& strSentence)
{
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
