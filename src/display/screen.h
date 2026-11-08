/*
 * Screen base class. Provides common functionality for display screens by delegating 
 * functionality to an underlying Display instance. This abstracts the basic primitives
 * such as drawing pixels, lines, rectangles, and text, allowing derived screen classes
 * to focus on higher-level layout and content management.
 *
 * (c) 2026 Erik Tkal
 *
 */

#pragma once

#include "display.h"
#include "font.h"

class Screen
{
public:
    Screen(Display::Shared spDisplay)
        : m_spDisplay(spDisplay)
    {
    }
    virtual ~Screen() = default;

protected:
    Display::Shared m_spDisplay;

public:
    // Screen management - delegates to m_spDisplay
    void SetFont(const BitmapFont* pFont)
    {
        m_spDisplay->SetFont(pFont);
    }
    const BitmapFont* GetFont() const
    {
        return m_spDisplay->GetFont();
    }
    void Clear()
    {
        m_spDisplay->Clear();
    }
    void Show()
    {
        m_spDisplay->Show();
    }
    uint16_t Width() const
    {
        return m_spDisplay->Width();
    }
    uint16_t Height() const
    {
        return m_spDisplay->Height();
    }
    void SetPixel(uint x, uint y, uint16_t color)
    {
        m_spDisplay->SetPixel(x, y, color);
    }
    void Ellipse(uint xCenter, uint yCenter, uint xRadius, uint yRadius, uint16_t color, bool bFill = false)
    {
        m_spDisplay->Ellipse(xCenter, yCenter, xRadius, yRadius, color, bFill);
    }
    void Line(uint x1, uint y1, uint x2, uint y2, uint16_t color)
    {
        m_spDisplay->Line(x1, y1, x2, y2, color);
    }
    void Text(const char* str, int x, int y, uint16_t color)
    {
        m_spDisplay->Text(str, x, y, color);
    }
    void Text(const char* str, int x, int y, uint16_t color, int scale)
    {
        m_spDisplay->Text(str, x, y, color, scale);
    }
    void Text(const char* str, int x, int y, uint16_t color, const BitmapFont& font, int scale = 1)
    {
        m_spDisplay->Text(str, x, y, color, font, scale);
    }
    void Fill(uint16_t color)
    {
        m_spDisplay->Fill(color);
    }
    void VLine(uint x, uint y, uint height, uint16_t color)
    {
        m_spDisplay->VLine(x, y, height, color);
    }
    void HLine(uint x, uint y, uint width, uint16_t color)
    {
        m_spDisplay->HLine(x, y, width, color);
    }
    void Rect(uint x, uint y, uint width, uint height, uint16_t color, bool bFill = false)
    {
        m_spDisplay->Rect(x, y, width, height, color, bFill);
    }
    void FillRect(uint x, uint y, uint width, uint height, uint16_t color)
    {
        m_spDisplay->FillRect(x, y, width, height, color);
    }

    // Get current font dimensions dynamically from display's font
    inline uint getCharWidth() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->width : 8;
    }
    inline uint getCharHeight() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->height : 8;
    }
    inline uint getLineAdvance() const
    {
        const BitmapFont* pFont = GetFont();
        return pFont ? pFont->effectiveLineAdvance() : 8;
    }
};