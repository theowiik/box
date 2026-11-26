// Smooth text: the Inter typeface, antialiased, at any size.
//
// Used by the `smooth` style. A text card is laid out as an optional title
// above the body: both word-wrapped, the body as large as fits, the title a
// smaller line above it. Text never scrolls; only text too long for the
// smallest size is cut, ending in "…".
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOX_FONT_BOLD,
    BOX_FONT_MEDIUM,
} box_font_face_t;

typedef enum {
    BOX_ALIGN_CENTER,
    BOX_ALIGN_LEFT,
} box_align_t;

/** Width in pixels of the code points at `size` (the font's em height). */
int box_font_width(box_font_face_t face, float size, const uint32_t *cps, size_t count);

/** Draw with the baseline's left end at (x, baseline), blending over the frame. */
void box_font_draw(box_frame_t *frame, box_font_face_t face, float size, const uint32_t *cps,
                   size_t count, int x, int baseline, box_rgb_t color);

#define BOX_CARD_LINES_MAX 24

typedef struct {
    size_t start, count;
} box_card_line_t;

/** A laid-out text card, ready to draw. */
typedef struct {
    float title_size, body_size;
    size_t title_lines, body_lines;
    box_card_line_t title[4];
    box_card_line_t body[BOX_CARD_LINES_MAX];
    bool truncated; /**< the body's last line is cut and needs "…" */
} box_card_layout_t;

/** Lay out `title` (may be empty) and `body` for a width x height area. */
void box_card_layout(const uint32_t *title, size_t title_count, const uint32_t *body,
                     size_t body_count, int width, int height, box_card_layout_t *out);

/** Draw a laid-out card centred vertically in the frame. With a title, the
 *  title takes `accent` and the body is white; without one, the body takes
 *  `accent`. */
void box_card_draw(box_frame_t *frame, const box_card_layout_t *layout, const uint32_t *title,
                   const uint32_t *body, box_align_t align, box_rgb_t accent);

#ifdef __cplusplus
}
#endif
