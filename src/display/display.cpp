/*
 * Display screen abstraction
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


#include "display.h"

void Display::initializeFramebuf(ePixelFormat eFormat, bool bReverseBytes)
{
    switch (m_nQuadrants)
    {
    case 1:
        Framebuf::Initialize(m_dispWidth, m_dispHeight, eFormat, bReverseBytes);
        quadrantList = {FULL_FRAME};
        break;
    case 2:
        if (m_dispWidth > m_dispHeight)
        {
            Framebuf::Initialize(m_dispWidth / 2, m_dispHeight, eFormat, bReverseBytes);
            quadrantList = {LEFT_HALF, RIGHT_HALF};
        }
        else
        {
            Framebuf::Initialize(m_dispWidth, m_dispHeight / 2, eFormat, bReverseBytes);
            quadrantList = {UPPER_HALF, LOWER_HALF};
        }
        break;
    case 4:
        Framebuf::Initialize(m_dispWidth / 2, m_dispHeight / 2, eFormat, bReverseBytes);
        quadrantList = {UPPER_LEFT, LOWER_LEFT, UPPER_RIGHT, LOWER_RIGHT};
        break;
    default:
        break;
    }
}

void Display::DoQuadrants(const std::function<void()>& drawAction)
{
    for (auto& quadrant : quadrantList)
    {
        SetQuadrant(quadrant);
        drawAction();
        Show();
    }
}

void Display::Clear()
{
    // Clear all quadrants by filling them with black
    DoQuadrants([this]() {
        // Just enumerating will trigger SetQuadrant and Show for each quadrant, effectively clearing them
    });
}

void Display::SetQuadrant(QUADRANT eQuadrant)
{
    m_eQuadrant = eQuadrant;
    switch (m_eQuadrant)
    {
    case FULL_FRAME:
    case LEFT_HALF:
    case UPPER_HALF:
    case UPPER_LEFT:
        SetOffset(0, 0);
        break;
    case RIGHT_HALF:
    case UPPER_RIGHT:
        SetOffset(m_dispWidth / 2, 0);
        break;
    case LOWER_HALF:
    case LOWER_LEFT:
        SetOffset(0, m_dispHeight / 2);
        break;
    case LOWER_RIGHT:
        SetOffset(m_dispWidth / 2, m_dispHeight / 2);
        break;
    }
    Fill(COLOUR_BLACK); // Clear previous buffer content for the new quadrant
}

std::list<QUADRANT> Display::GetQuadrants()
{
    return quadrantList;
}

