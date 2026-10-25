/*
 * Display device base class.
 *
 * (c) 2026 Erik Tkal
 *
 */

#pragma once

#include <memory>
#include <cstdint>
#include <list>
#include <functional>

#include "framebuf.h"

enum ROTATION
{
    R0DEG,
    R90DEG,
    R180DEG,
    R270DEG,
    MIRRORED0DEG,
    MIRRORED90DEG,
    MIRRORED180DEG,
    MIRRORED270DEG
};

enum QUADRANT
{
    FULL_FRAME,
    LEFT_HALF,
    RIGHT_HALF,
    UPPER_HALF,
    LOWER_HALF,
    UPPER_LEFT,
    LOWER_LEFT,
    UPPER_RIGHT,
    LOWER_RIGHT
};


class Display : public std::enable_shared_from_this<Display>, public Framebuf
{
public:
    typedef std::shared_ptr<Display> Shared;

    Display(uint16_t dispWidth, uint16_t dispHeight)
        : m_dispWidth(dispWidth),
          m_dispHeight(dispHeight)
    {
    }
    Display() = delete;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    virtual ~Display() = default;

    virtual void Initialize() = 0;
    virtual void Clear();
    virtual void Show() = 0;
    virtual uint RecommendedFontSize() const = 0;

    void SetQuadrant(QUADRANT eQuadrant);
    std::list<QUADRANT> GetQuadrants();
    void DoQuadrants(const std::function<void()>& drawAction);

    uint16_t Width()
    {
        return m_dispWidth;
    }
    uint16_t Height()
    {
        return m_dispHeight;
    }

    bool Landscape() const
    {
        return m_dispWidth >= m_dispHeight;
    }
    bool Portrait() const
    {
        return !Landscape();
    }
    uint16_t ShorterSide() const
    {
        return (m_dispWidth < m_dispHeight) ? m_dispWidth : m_dispHeight;
    }

protected:
    void initializeFramebuf(ePixelFormat eFormat = DISPLAY_COLOUR_FORMAT, bool bReverseBytes = false);

    uint16_t m_dispWidth;
    uint16_t m_dispHeight;

    uint16_t m_nQuadrants;
    std::list<QUADRANT> quadrantList;
    QUADRANT m_eQuadrant;
};