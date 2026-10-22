/*
 * XPT2046 resistive touch controller (e.g. Waveshare Pico-ResTouch-LCD-3.5)
 *
 * Copyright (c) 2026 Erik Tkal
 *
 * Partially derived from Waveshare sample code, though this implementation varies considerably
 * in that it uses an IRQ handler for initial touch notification and then performs the sampling,
 * rather than continually polling the IRQ pin.  In addition, the timing and sensitivity of
 * the touch handling is optimized based on my own experimentation with finger presses, etc.
 *
 * The controller shares the SPI bus with the display, so this class is designed to stay out
 * of the way of the display:
 *
 *  - The touch IRQ pin (active low) triggers a GPIO interrupt handler. The handler only masks
 *      further interrupts from the pin and sets a flag; no SPI traffic occurs in the interrupt
 *      handler context.
 *  - DoWork() must be called regularly from the same thread/core that drives the display. When the
 *      flag is set, it samples the touch position at a modest interval (never blocking) until the
 *      touch is released, then re-arms the interrupt.
 *  - Touches are reported as button-like events (Down, Up, Tap, LongPress) carrying screen
 *      coordinates, via a queue_t and/or an optional callback.
 */

#pragma once

#include <memory>
#include <vector>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/spi.h"

#include "display.h"

enum class TouchEventType : uint8_t
{
    Down,     // Finger/stylus made contact
    Up,       // Contact released (always follows Down)
    Tap,      // Released after a short contact with little movement
    LongPress // Contact held past the long press time (fired once, while held)
};

struct TouchEvent
{
    TouchEventType eType {TouchEventType::Down};
    uint16_t x {0}; // Screen coordinate in display pixels
    uint16_t y {0};
    uint32_t nDurationMs {0}; // Time since Down
    bool bHasArea {false};    // True if the position is inside a registered area
    uint nAreaId {0};         // ID of the highest priority registered area containing the position
};

typedef void (*touchEventCallback)(void* pCtx, const TouchEvent& event);

// Maps raw 12-bit ADC readings to screen coordinates, in "reference pixels" of a reference
// resolution which is then scaled to the actual display size:
//   x = xFac * (swapXY ? rawY : rawX) + xOff
//   y = yFac * (swapXY ? rawX : rawY) + yOff
struct TouchCalibration
{
    bool bSwapXY {false};
    float xFac {0};
    float yFac {0};
    float xOff {0};
    float yOff {0};
    uint16_t nRefWidth {480};
    uint16_t nRefHeight {320};
};

class Touch : public std::enable_shared_from_this<Touch>
{
public:
    typedef std::shared_ptr<Touch> Shared;

    Touch(spi_inst_t* spi, uint nCsPin, uint nIrqPin, ROTATION rotation, uint nSpiBaud = 2000000);
    Touch(spi_inst_t* spi, uint nMisoPin, uint nMosiPin, uint nSckPin, uint nCsPin, uint nIrqPin, ROTATION rotation, uint nSpiBaud = 2000000);
    ~Touch();

    // Must be called on the core which will call DoWork() (the GPIO interrupt is per core), after the
    // display (and SPI bus) has been initialized.
    void Initialize(uint16_t nDispWidth, uint16_t nDispHeight);

    // Process pending touch work; non-blocking. Call from the display processing loop.
    void DoWork();

    // Retrieve the next queued event. Returns false if none are available.
    bool GetEvent(TouchEvent& event);

    // Optional: callback invoked from DoWork() (same context) in addition to queuing the event.
    void SetEventCallback(void* pCtx, touchEventCallback pCB);

    // Override the default calibration (see TouchCalibration).
    void SetCalibration(const TouchCalibration& cal);

    // Register a clickable rectangular area, inclusive of its boundary coordinates. Areas are checked in the
    // order they were added, so earlier areas take priority where they overlap. Registering an ID which already
    // exists updates its rectangle and keeps its priority. Call from the same core as DoWork().
    void AddArea(uint nId, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    // Register a circular area by center and radius (inclusive); shares the ID space and priority order of
    // rectangular areas.
    void AddCircleArea(uint nId, uint16_t xCenter, uint16_t yCenter, uint16_t nRadius);
    void RemoveArea(uint nId);
    void ClearAreas();
    // Find the highest priority registered area containing the point. Returns false if there is none.
    bool FindArea(uint16_t x, uint16_t y, uint& nId) const;

    // Map a point to a cell index (row-major) of a nCols x nRows grid over the screen.
    uint GetGridCell(uint16_t x, uint16_t y, uint nCols, uint nRows) const;

private:
    enum class State
    {
        Idle,
        Sampling
    };

    static void irqHandler();
    void armIrq();

    uint16_t readAdc(uint8_t nCmd);
    uint16_t readAdcAverage(uint8_t nCmd);
    bool readRaw(uint16_t& nRawX, uint16_t& nRawY);
    void mapToScreen(uint16_t nRawX, uint16_t nRawY, uint16_t& x, uint16_t& y) const;
    void defaultCalibration();
    void postDown();
    void postEvent(TouchEventType eType, uint32_t nDurationMs);

    bool m_bSharedSpi {true};
    spi_inst_t* m_spi {nullptr};
    uint m_nMisoPin {0};
    uint m_nMosiPin {0};
    uint m_nSckPin {0};
    uint m_nCsPin {0};
    uint m_nIrqPin {0};
    ROTATION m_rotation;
    uint m_nSpiBaud;

    uint16_t m_nDispWidth {0};
    uint16_t m_nDispHeight {0};
    TouchCalibration m_cal;

    State m_eState {State::Idle};
    volatile bool m_bIrqPending {false};

    uint32_t m_nDownTimeMs {0};
    uint32_t m_nNextSampleMs {0};
    uint16_t m_nStartX {0};
    uint16_t m_nStartY {0};
    uint16_t m_nLastX {0};
    uint16_t m_nLastY {0};
    bool m_bDown {false};
    uint16_t m_nStartRawX {0};
    uint16_t m_nStartRawY {0};
    bool m_bDownPosted {false}; // Down event is deferred until the contact position settles
    bool m_bLongPressFired {false};
    uint m_nReleaseCount {0};
    uint m_nInvalidCount {0};
    bool m_bMoved {false};
    uint m_nFarCount {0}; // Consecutive samples away from the start position

    struct Area
    {
        uint nId;
        bool bCircle;
        uint16_t x0; // Rectangle bounds, or the circle's bounding box
        uint16_t y0;
        uint16_t x1;
        uint16_t y1;
        uint16_t xc; // Circle center and radius
        uint16_t yc;
        uint16_t r;
        bool Contains(uint16_t x, uint16_t y) const;
    };
    void setArea(const Area& area);
    std::vector<Area> m_areas; // In priority order

    queue_t m_qEvents;
    void* m_pCtx {nullptr};
    touchEventCallback m_pCB {nullptr};

    static Touch* sm_pInstance;
};
