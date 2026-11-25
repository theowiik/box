// Scenes: things that draw themselves into a frame over time.
//
// Scenes are immutable once built and reference-counted: the scheduler, an
// ambient card and the render engine (mid-crossfade) can all hold the same
// one. Retain/release are not atomic; callers serialize them (the device
// does every scene operation under one lock).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOX_SCENE_BLANK,
    BOX_SCENE_SOLID,
    BOX_SCENE_TEXT,
    BOX_SCENE_FRAMES,
} box_scene_kind_t;

typedef struct box_scene box_scene_t;

/* Constructors return a scene with one reference, or NULL when out of memory. */
box_scene_t *box_scene_new_blank(void);
box_scene_t *box_scene_new_solid(box_rgb_t color);
/** Copies the UTF-8 text. It is wrapped and sized to fit the panel (see
 *  box_text_layout); '\n' forces a line break. */
box_scene_t *box_scene_new_text(const char *utf8, size_t len, box_rgb_t color);
/** Takes ownership of `data` (freed with free()) whether or not it succeeds.
 *  `size` must be a whole number of width * height * 3 frames. Frames of a
 *  different size than the panel are nearest-neighbour scaled when drawn. */
box_scene_t *box_scene_new_frames(uint8_t *data, size_t size, int width, int height,
                                  float fps, bool loop);

box_scene_t *box_scene_retain(box_scene_t *scene);
void box_scene_release(box_scene_t *scene);

box_scene_kind_t box_scene_kind(const box_scene_t *scene);
/** "blank", "solid", "text" or "frames". */
const char *box_scene_kind_name(const box_scene_t *scene);
/** The UTF-8 text of a text scene, NULL for any other kind. */
const char *box_scene_text(const box_scene_t *scene);

/** Draw at `t` seconds since the scene became active. */
void box_scene_render(box_scene_t *scene, box_frame_t *frame, double t);

/* Animation memory budget. Frame data lives as long as some queue entry,
 * ambient card or crossfade holds it, so it is bounded as a whole rather than
 * per upload: a burst of large uploads must not exhaust the device. */
void box_frames_set_budget(size_t bytes);
/** True when `bytes` more frame data fits in the budget right now. */
bool box_frames_budget_allows(size_t bytes);
size_t box_frames_in_use(void);

#ifdef __cplusplus
}
#endif
