#include <vector>
#include <cstdlib>
#include <cstring>
#include "font_factory.h"
#include "font.h"
#include "font_petme128_8x8.h"
#include "font_terminus_6x12.h"
#include "font_terminus_8x14.h"
#include "font_terminus_10x18.h"
#include "font_terminus_12x24.h"
#include "font_terminus_16x32.h"

// Bitmap fonts sorted by height (PetMe + Terminus bold)
// Keyed by pixel height
static BitmapFont bitmap_fonts[] = {
    { font_petme128_8x8,   8,  8, 32, 96, 1,  8 },  // PetMe 8x8 (column-major)
    { font_terminus_6x12,  6, 12, 32, 96, 0, 10 },  // Terminus 6x12 (row-major, tighter line advance)
    { font_terminus_8x14,  8, 14, 32, 96, 0, 12 },  // Terminus 8x14 (row-major, tighter line advance)
    { font_terminus_10x18, 10, 18, 32, 96, 0, 15 },  // Terminus 10x18 (row-major, tighter line advance)
    { font_terminus_12x24, 12, 24, 32, 96, 0, 20 },  // Terminus 12x24 (row-major, tighter line advance)
    { font_terminus_16x32, 16, 32, 32, 96, 0, 27 },  // Terminus 16x32 (row-major, tighter line advance)
};
static const int bitmap_heights[] = { 8, 12, 14, 18, 24, 32 };
static const int bitmap_count = sizeof(bitmap_fonts) / sizeof(bitmap_fonts[0]);

const BitmapFont* get_terminus_font(int height)
{
    // Get font by pixel height (8, 12, 14, 18, 24, or 32)
    for (int i = 0; i < bitmap_count; ++i)
        if (bitmap_heights[i] == height)
            return &bitmap_fonts[i];
    return nullptr;
}
