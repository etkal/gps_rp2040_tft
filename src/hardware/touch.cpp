/*
 * XPT2046 resistive touch controller. See touch.h for details.
 *
 * Copyright (c) 2026 Erik Tkal
 *
 * Derived from the Waveshare LCD_Touch sample code.
 */

#include "touch.h"

#include <stdlib.h>
#include <algorithm>
#include <string>

#include "hardware/gpio.h"
#include "hardware/irq.h"

#include "log.h"

namespace
{
    constexpr uint8_t CMD_READ_X = 0xD0; // X+ position, 12-bit, differential
    constexpr uint8_t CMD_READ_Y = 0x90; // Y+ position, 12-bit, differential

    constexpr uint16_t RAW_MIN = 20; // ADC readings at or beyond these are saturated
    constexpr uint16_t RAW_MAX = 4075;

    constexpr uint READ_TIMES = 5; // Samples per axis
    constexpr uint LOST_NUM = 1;   // Samples discarded at each end after sorting
    constexpr int ERR_RANGE = 120; // Maximum deviation between two readings (fingers are noisier than a stylus)

    constexpr uint32_t SAMPLE_INTERVAL_MS = 15; // Spacing of position samples while touched
    constexpr uint RELEASE_COUNT = 4;           // Consecutive "not touched" polls to confirm release
    constexpr uint MAX_INVALID = 30;            // Failed reads before giving up on a spurious interrupt
    constexpr uint32_t TAP_MAX_MS = 700;
    constexpr uint READ_ATTEMPTS = 3; // Immediate retries of a noisy position read
    constexpr uint32_t LONG_PRESS_MS = 1500;
    constexpr int MOVE_THRESHOLD_PX = 60;
    constexpr uint MOVE_SAMPLES = 4;    // Consecutive far samples needed to call it movement (rejects lift-off noise)
    constexpr uint32_t SETTLE_MS = 100; // Contact pressure ramps up; positions during this period are unreliable
    constexpr uint QUEUE_DEPTH = 8;

    inline uint32_t nowMs()
    {
        return to_ms_since_boot(get_absolute_time());
    }
} // namespace

Touch* Touch::sm_pInstance = nullptr;

Touch::Touch(spi_inst_t* spi, uint nCsPin, uint nIrqPin, ROTATION rotation, uint nSpiBaud)
    : m_bSharedSpi(true),
      m_spi(spi),
      m_nCsPin(nCsPin),
      m_nIrqPin(nIrqPin),
      m_rotation(rotation),
      m_nSpiBaud(nSpiBaud)
{
    queue_init(&m_qEvents, sizeof(TouchEvent), QUEUE_DEPTH);
}

Touch::Touch(spi_inst_t* spi, uint nMisoPin, uint nMosiPin, uint nSckPin, uint nCsPin, uint nIrqPin, ROTATION rotation, uint nSpiBaud)
    : m_bSharedSpi(false),
      m_spi(spi),
      m_nMisoPin(nMisoPin),
      m_nMosiPin(nMosiPin),
      m_nSckPin(nSckPin),
      m_nCsPin(nCsPin),
      m_nIrqPin(nIrqPin),
      m_rotation(rotation),
      m_nSpiBaud(nSpiBaud)
{
    queue_init(&m_qEvents, sizeof(TouchEvent), QUEUE_DEPTH);
}

Touch::~Touch()
{
    if (this == sm_pInstance)
    {
        gpio_set_irq_enabled(m_nIrqPin, GPIO_IRQ_EDGE_FALL, false);
        gpio_remove_raw_irq_handler_masked(1u << m_nIrqPin, &Touch::irqHandler);
        sm_pInstance = nullptr;
    }
    queue_free(&m_qEvents);
}

void Touch::defaultCalibration()
{
    // Waveshare Pico-ResTouch-LCD-3.5 factors. The mapping from display rotation to touch panel
    // orientation depends on how the panel is mounted; adjust with SetCalibration() if needed.
    switch (m_rotation % 4)
    {
    case 0: // R0DEG
        m_cal = {false, -0.0897f, -0.1348f, 341.0f, 519.0f, 320, 480};
        break;
    case 1: // R90DEG
        m_cal = {true, -0.1329f, 0.0880f, 517.0f, -20.0f, 480, 320};
        break;
    case 2: // R180DEG
        m_cal = {false, 0.0899f, 0.1332f, -22.0f, -38.0f, 320, 480};
        break;
    default: // R270DEG
        m_cal = {true, 0.1324f, -0.0900f, -36.0f, 342.0f, 480, 320};
        break;
    }
}

void Touch::SetCalibration(const TouchCalibration& cal)
{
    m_cal = cal;
}

void Touch::Initialize(uint16_t nDispWidth, uint16_t nDispHeight)
{
    m_nDispWidth = nDispWidth;
    m_nDispHeight = nDispHeight;
    defaultCalibration();

    if (!m_bSharedSpi)
    {
        // If we have our own SPI buss initialize it and configure the pins
        spi_init(m_spi, m_nSpiBaud);
        gpio_set_function(m_nMisoPin, GPIO_FUNC_SPI);
        gpio_set_function(m_nMosiPin, GPIO_FUNC_SPI);
        gpio_set_function(m_nSckPin, GPIO_FUNC_SPI);
    }

    gpio_init(m_nCsPin);
    gpio_set_dir(m_nCsPin, GPIO_OUT);
    gpio_put(m_nCsPin, 1);

    gpio_init(m_nIrqPin);
    gpio_set_dir(m_nIrqPin, GPIO_IN);
    gpio_pull_up(m_nIrqPin);

    sm_pInstance = this;

    // A raw handler coexists with any gpio callback (e.g. Button) registered on this core.
    gpio_add_raw_irq_handler_masked(1u << m_nIrqPin, &Touch::irqHandler);
    irq_set_enabled(IO_IRQ_BANK0, true);
    armIrq();

    LogInfo("Touch initialized");
}

// Runs in interrupt context: only mask the pin and flag the work; no SPI access here.
void Touch::irqHandler()
{
    Touch* pThis = sm_pInstance;
    if (!pThis)
    {
        return;
    }
    uint32_t nEvents = gpio_get_irq_event_mask(pThis->m_nIrqPin);
    if (nEvents)
    {
        gpio_acknowledge_irq(pThis->m_nIrqPin, nEvents);
        if (nEvents & GPIO_IRQ_EDGE_FALL)
        {
            gpio_set_irq_enabled(pThis->m_nIrqPin, GPIO_IRQ_EDGE_FALL, false);
            pThis->m_bIrqPending = true;
        }
    }
}

void Touch::armIrq()
{
    gpio_set_irq_enabled(m_nIrqPin, GPIO_IRQ_EDGE_FALL, true);
    // A touch which began before the interrupt was re-armed produces no edge, so catch it here.
    if (!gpio_get(m_nIrqPin))
    {
        gpio_set_irq_enabled(m_nIrqPin, GPIO_IRQ_EDGE_FALL, false);
        m_bIrqPending = true;
    }
}

uint16_t Touch::readAdc(uint8_t nCmd)
{
    uint8_t tx[3] = {nCmd, 0, 0};
    uint8_t rx[3] = {0, 0, 0};

    gpio_put(m_nCsPin, 0);
    spi_write_read_blocking(m_spi, tx, rx, sizeof(tx));
    gpio_put(m_nCsPin, 1);

    return (((static_cast<uint16_t>(rx[1]) << 8) | rx[2]) >> 3) & 0x0FFF;
}

// Sort several readings and average the middle ones to reject noise.
uint16_t Touch::readAdcAverage(uint8_t nCmd)
{
    uint16_t buff[READ_TIMES];
    for (uint i = 0; i < READ_TIMES; ++i)
    {
        buff[i] = readAdc(nCmd);
    }
    for (uint i = 0; i < READ_TIMES - 1; ++i)
    {
        for (uint j = i + 1; j < READ_TIMES; ++j)
        {
            if (buff[i] > buff[j])
            {
                uint16_t tmp = buff[i];
                buff[i] = buff[j];
                buff[j] = tmp;
            }
        }
    }
    uint32_t nSum = 0;
    for (uint i = LOST_NUM; i < READ_TIMES - LOST_NUM; ++i)
    {
        nSum += buff[i];
    }
    return static_cast<uint16_t>(nSum / (READ_TIMES - 2 * LOST_NUM));
}

// Read the position twice, accepting it only if both readings agree.
bool Touch::readRaw(uint16_t& nRawX, uint16_t& nRawY)
{
    // The touch controller needs a slower clock than the display. If we are sharing the SPI bus,
    // temporarily lower its speed and restore the display speed afterwards.
    uint nPrevBaud;
    if (m_bSharedSpi)
    {
        nPrevBaud = spi_get_baudrate(m_spi);
        spi_set_baudrate(m_spi, m_nSpiBaud);
    }

    uint16_t x1 = readAdcAverage(CMD_READ_X);
    uint16_t y1 = readAdcAverage(CMD_READ_Y);
    uint16_t x2 = readAdcAverage(CMD_READ_X);
    uint16_t y2 = readAdcAverage(CMD_READ_Y);

    if (m_bSharedSpi)
    {
        spi_set_baudrate(m_spi, nPrevBaud);
    }

    if (abs(static_cast<int>(x1) - static_cast<int>(x2)) < ERR_RANGE && abs(static_cast<int>(y1) - static_cast<int>(y2)) < ERR_RANGE)
    {
        nRawX = (x1 + x2) / 2;
        nRawY = (y1 + y2) / 2;

#if !defined(DISPLAY_PICO_RESTOUCH)
        // Invert the X coordinate for non-Pico-ResTouch displays
        nRawX = 4095 - nRawX;
#endif
        // Rail values indicate no valid contact yet (e.g. pressure still ramping up)
        return nRawX > RAW_MIN && nRawX < RAW_MAX && nRawY > RAW_MIN && nRawY < RAW_MAX;
    }
    return false;
}

void Touch::mapToScreen(uint16_t nRawX, uint16_t nRawY, uint16_t& x, uint16_t& y) const
{
    float fX = m_cal.xFac * (m_cal.bSwapXY ? nRawY : nRawX) + m_cal.xOff;
    float fY = m_cal.yFac * (m_cal.bSwapXY ? nRawX : nRawY) + m_cal.yOff;

    fX = fX * m_nDispWidth / m_cal.nRefWidth;
    fY = fY * m_nDispHeight / m_cal.nRefHeight;

    if (fX < 0)
    {
        fX = 0;
    }
    if (fY < 0)
    {
        fY = 0;
    }
    if (fX > m_nDispWidth - 1)
    {
        fX = m_nDispWidth - 1;
    }
    if (fY > m_nDispHeight - 1)
    {
        fY = m_nDispHeight - 1;
    }
    x = static_cast<uint16_t>(fX);
    y = static_cast<uint16_t>(fY);
}

void Touch::postEvent(TouchEventType eType, uint32_t nDurationMs)
{
    TouchEvent event;
    event.eType = eType;
    event.x = m_nLastX;
    event.y = m_nLastY;
    event.nDurationMs = nDurationMs;
    event.bHasArea = FindArea(event.x, event.y, event.nAreaId);

    queue_try_add(&m_qEvents, &event); // Drop the event if the consumer is not keeping up
    if (m_pCB)
    {
        m_pCB(m_pCtx, event);
    }
}

void Touch::postDown()
{
    m_bDownPosted = true;
    m_nLastX = m_nStartX;
    m_nLastY = m_nStartY;
    LogInfoD("Touch down: raw " + std::to_string(m_nStartRawX) + "," + std::to_string(m_nStartRawY) + " screen " +
             std::to_string(m_nStartX) + "," + std::to_string(m_nStartY));
    postEvent(TouchEventType::Down, 0);
}

void Touch::DoWork()
{
    if (State::Idle == m_eState)
    {
        if (!m_bIrqPending)
        {
            return;
        }
        m_bIrqPending = false;
        m_eState = State::Sampling;
        m_nNextSampleMs = nowMs();
        m_bDown = false;
        m_bDownPosted = false;
        m_bMoved = false;
        m_nFarCount = 0;
        m_bLongPressFired = false;
        m_nReleaseCount = 0;
        m_nInvalidCount = 0;
    }

    uint32_t nNow = nowMs();
    if (static_cast<int32_t>(nNow - m_nNextSampleMs) < 0)
    {
        return; // Not time to sample yet; leave the SPI bus to the display
    }
    m_nNextSampleMs = nNow + SAMPLE_INTERVAL_MS;

    if (!gpio_get(m_nIrqPin)) // Active low: touched
    {
        m_nReleaseCount = 0;
        uint16_t nRawX, nRawY, x, y;
        bool bValid = false;
        for (uint i = 0; i < READ_ATTEMPTS && !bValid; ++i)
        {
            bValid = readRaw(nRawX, nRawY);
        }
        if (bValid)
        {
            m_nInvalidCount = 0;
            mapToScreen(nRawX, nRawY, x, y);
            m_nLastX = x;
            m_nLastY = y;
            if (!m_bDown)
            {
                m_bDown = true;
                m_nDownTimeMs = nNow;
                m_nStartX = x;
                m_nStartY = y;
                m_nStartRawX = nRawX;
                m_nStartRawY = nRawY;
            }
            else if ((nNow - m_nDownTimeMs) < SETTLE_MS)
            {
                // Still settling: track the latest reading as the reference point instead of treating it as movement
                m_nStartX = x;
                m_nStartY = y;
                m_nStartRawX = nRawX;
                m_nStartRawY = nRawY;
            }
            else
            {
                if (!m_bDownPosted)
                {
                    postDown();
                }
                if (abs(static_cast<int>(x) - m_nStartX) > MOVE_THRESHOLD_PX || abs(static_cast<int>(y) - m_nStartY) > MOVE_THRESHOLD_PX)
                {
                    if (++m_nFarCount >= MOVE_SAMPLES)
                    {
                        m_bMoved = true;
                    }
                }
                else
                {
                    m_nFarCount = 0;
                }
                if (!m_bLongPressFired && (nNow - m_nDownTimeMs) >= LONG_PRESS_MS)
                {
                    m_bLongPressFired = true;
                    postEvent(TouchEventType::LongPress, nNow - m_nDownTimeMs);
                }
            }
        }
        else if (!m_bDown && ++m_nInvalidCount >= MAX_INVALID)
        {
            m_eState = State::Idle; // Spurious interrupt
            armIrq();
        }
        return;
    }

    // Not touched; require consecutive polls to ignore momentary dropouts
    if (++m_nReleaseCount < RELEASE_COUNT)
    {
        return;
    }
    if (m_bDown)
    {
        // The reading at lift-off is unreliable, so report the settled contact position
        m_nLastX = m_nStartX;
        m_nLastY = m_nStartY;
        if (!m_bDownPosted)
        {
            postDown(); // Released before settling; still report the Down ahead of Up/Tap
        }
        uint32_t nDuration = nNow - m_nDownTimeMs;
        LogInfoD("Touch up: duration " + std::to_string(nDuration) + "ms moved " + std::to_string(m_bMoved) + " long " +
                 std::to_string(m_bLongPressFired));
        postEvent(TouchEventType::Up, nDuration);
        if (!m_bLongPressFired && !m_bMoved && nDuration < TAP_MAX_MS)
        {
            postEvent(TouchEventType::Tap, nDuration);
        }
        m_bDown = false;
    }
    m_eState = State::Idle;
    armIrq();
}

bool Touch::GetEvent(TouchEvent& event)
{
    return queue_try_remove(&m_qEvents, &event);
}

void Touch::SetEventCallback(void* pCtx, touchEventCallback pCB)
{
    m_pCtx = pCtx;
    m_pCB = pCB;
}

bool Touch::Area::Contains(uint16_t x, uint16_t y) const
{
    if (x < x0 || x > x1 || y < y0 || y > y1)
    {
        return false;
    }
    if (!bCircle)
    {
        return true;
    }
    int32_t dx = static_cast<int32_t>(x) - xc;
    int32_t dy = static_cast<int32_t>(y) - yc;
    return (dx * dx + dy * dy) <= static_cast<int32_t>(r) * r;
}

// Replace an existing area with the same ID in place (keeping its priority), otherwise append
void Touch::setArea(const Area& area)
{
    for (auto& existing : m_areas)
    {
        if (existing.nId == area.nId)
        {
            existing = area;
            return;
        }
    }
    m_areas.push_back(area);
}

void Touch::AddArea(uint nId, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    setArea({nId, false, std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1), 0, 0, 0});
}

void Touch::AddCircleArea(uint nId, uint16_t xCenter, uint16_t yCenter, uint16_t nRadius)
{
    uint16_t xMin = xCenter > nRadius ? xCenter - nRadius : 0;
    uint16_t yMin = yCenter > nRadius ? yCenter - nRadius : 0;
    setArea({nId,
             true,
             xMin,
             yMin,
             static_cast<uint16_t>(xCenter + nRadius),
             static_cast<uint16_t>(yCenter + nRadius),
             xCenter,
             yCenter,
             nRadius});
}

void Touch::RemoveArea(uint nId)
{
    for (auto it = m_areas.begin(); it != m_areas.end(); ++it)
    {
        if (it->nId == nId)
        {
            m_areas.erase(it);
            return;
        }
    }
}

void Touch::ClearAreas()
{
    m_areas.clear();
}

bool Touch::FindArea(uint16_t x, uint16_t y, uint& nId) const
{
    for (const auto& area : m_areas)
    {
        if (area.Contains(x, y))
        {
            nId = area.nId;
            return true;
        }
    }
    return false;
}

uint Touch::GetGridCell(uint16_t x, uint16_t y, uint nCols, uint nRows) const
{
    if (0 == nCols || 0 == nRows || 0 == m_nDispWidth || 0 == m_nDispHeight)
    {
        return 0;
    }
    uint nCol = (static_cast<uint>(x) * nCols) / m_nDispWidth;
    uint nRow = (static_cast<uint>(y) * nRows) / m_nDispHeight;
    if (nCol >= nCols)
    {
        nCol = nCols - 1;
    }
    if (nRow >= nRows)
    {
        nRow = nRows - 1;
    }
    return nRow * nCols + nCol;
}
