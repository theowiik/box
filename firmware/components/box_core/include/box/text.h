// The 5x7 bitmap font, UTF-8 handling, and fitting text onto the panel.
//
// Text never scrolls: it is word-wrapped and drawn at the largest whole
// multiple of the font that fits, and only text too long for even the
// smallest size is cut, ending in "...".
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOX_GLYPH_WIDTH 5
#define BOX_GLYPH_HEIGHT 7
/** Glyph plus one blank column, at scale 1. */
#define BOX_GLYPH_ADVANCE (BOX_GLYPH_WIDTH + 1)
/** Glyph plus one blank row between lines, at scale 1. */
#define BOX_LINE_ADVANCE (BOX_GLYPH_HEIGHT + 1)
/** Blank dots kept between text and the panel edge. */
#define BOX_TEXT_MARGIN 2
#define BOX_TEXT_LINES_MAX 32

/** Row masks (BOX_GLYPH_HEIGHT of them, bit 4 = leftmost column) for a code
 *  point. Lowercase maps to uppercase; anything unmapped draws as '?'. */
const uint8_t *box_font_glyph(uint32_t codepoint);

/** Width in dots of `count` glyphs at `scale`, without the trailing gap. */
int box_text_width(size_t count, int scale);

/** Number of code points in UTF-8 text. Malformed bytes count as one each. */
size_t box_utf8_count(const char *text, size_t len);

/** Decode UTF-8 into at most `max` code points; malformed bytes become
 *  U+FFFD (drawn as '?'). Returns how many were written. */
size_t box_utf8_decode(const char *text, size_t len, uint32_t *out, size_t max);

/** Draw code points with the top-left of the first glyph at (x, y), each font
 *  dot as a scale x scale block. Anything off the frame is clipped. */
void box_frame_draw_text(box_frame_t *frame, const uint32_t *codepoints, size_t count,
                         int x, int y, int scale, box_rgb_t color);

typedef struct {
    size_t start; /**< Index of the line's first code point. */
    size_t count;
} box_text_line_t;

typedef struct {
    int scale;
    size_t line_count;
    box_text_line_t lines[BOX_TEXT_LINES_MAX];
    /** The text did not fit at scale 1: the last line is cut short and
     *  should be followed by "...". */
    bool truncated;
} box_text_layout_t;

/** Word-wrap text for a width x height panel at the largest scale that fits.
 *  Lines break at spaces, at '\n', or mid-word when a word is too long. */
void box_text_layout(const uint32_t *codepoints, size_t count, int width, int height,
                     box_text_layout_t *out);

#ifdef __cplusplus
}
#endif
