// The logical panel: a small RGB pixel grid every scene draws into.
//
// Portable C with no ESP-IDF dependency, so the whole core builds and is
// tested on any machine (see core/CMakeLists.txt).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r, g, b;
} box_rgb_t;

/** Parse "#RRGGBB", "RRGGBB", "#RGB" or "RGB" (case-insensitive). */
bool box_rgb_parse(const char *text, box_rgb_t *out);

/** Format as "#RRGGBB" (7 characters plus NUL). */
void box_rgb_format(box_rgb_t color, char out[8]);

/** A rectangle of pixels, corners inclusive. */
typedef struct {
    int x1, y1, x2, y2;
} box_rect_t;

/** A width x height RGB pixel buffer, row-major from the top-left. */
typedef struct {
    int width;
    int height;
    uint8_t *pixels; /**< width * height * 3 bytes; owned by the frame. */
    /** Where the frame may differ from the last one shown: the whole frame
     *  unless whoever drew it knows better. Lets a display skip the rest. */
    box_rect_t changed;
} box_frame_t;

/** Allocate a black frame. Returns false (frame zeroed) on allocation failure. */
bool box_frame_init(box_frame_t *frame, int width, int height);
void box_frame_free(box_frame_t *frame);
size_t box_frame_bytes(const box_frame_t *frame);

void box_frame_clear(box_frame_t *frame, box_rgb_t color);
/** Out-of-bounds coordinates are ignored. */
void box_frame_set_px(box_frame_t *frame, int x, int y, box_rgb_t color);
/** Blend color over the pixel; alpha is clamped to 0..1, out of bounds ignored. */
void box_frame_blend_px(box_frame_t *frame, int x, int y, box_rgb_t color, float alpha);

/** Mark the whole frame as changed. */
void box_frame_changed_all(box_frame_t *frame);

#ifdef __cplusplus
}
#endif
