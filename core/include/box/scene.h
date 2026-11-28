// Scenes: content that draws itself.
//
// A scene is what an event asked to show — text, an image, a colour — and
// knows how to draw itself in either style:
//
//   smooth  onto the full screen: Inter text, images scaled to fit
//   led     onto a coarse grid shown as square LEDs: the 5x7 bitmap font
//
// Scenes are immutable once shared and reference-counted: the scheduler, an
// ambient card and the render engine can all hold the same one. Retain and
// release are not atomic; callers serialize them (the device does every
// scene operation under one lock).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "box/font.h"
#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOX_STYLE_DEFAULT, /**< whatever the display is set to */
    BOX_STYLE_SMOOTH,
    BOX_STYLE_LED,
} box_style_t;

typedef enum {
    BOX_SCALING_PIXELATED, /**< hard-edged blocks: pixel art stays pixel art */
    BOX_SCALING_SMOOTH,    /**< bilinear: photos and soft gradients */
} box_scaling_t;

typedef enum {
    BOX_CONTENT_CLEAR,
    BOX_CONTENT_COLOR,
    BOX_CONTENT_TEXT,
    BOX_CONTENT_IMAGE,
    BOX_CONTENT_CLOCK,
    BOX_CONTENT_GLANCE,
} box_content_t;

/** The local time for a clock. False while it is not known yet. */
typedef bool (*box_clock_fn)(void *ctx, int *hour, int *minute, int *second);

typedef struct box_scene box_scene_t;

/* Constructors return a scene with one reference, or NULL when out of memory. */
box_scene_t *box_scene_new_clear(void);
box_scene_t *box_scene_new_color(box_rgb_t color);
/** Copies the UTF-8. `title` may be empty. Text is wrapped and sized to fit,
 *  never scrolled; '\n' forces a line break. */
box_scene_t *box_scene_new_text(const char *title, size_t title_len, const char *text,
                                size_t text_len, box_rgb_t color, box_align_t align);
/** Takes ownership of `data` (freed with free()) whether or not it succeeds:
 *  a whole number of width x height RGB frames, played at `fps` and scaled to
 *  fit the screen, centred. */
box_scene_t *box_scene_new_image(uint8_t *data, size_t size, int width, int height, float fps,
                                 bool loop, box_scaling_t scaling);
/** HH:MM, sized like any text; empty while `now` cannot tell the time. */
box_scene_t *box_scene_new_clock(box_clock_fn now, void *ctx, box_rgb_t color);

/** The time, the weather and a smiley on one screen. Copies the UTF-8.
 *  `sky` and `mood` index the names in event.h (BOX_GLANCE_SKIES/MOODS). */
box_scene_t *box_scene_new_glance(const char *date, const char *time, const char *temp,
                                  const char *condition, int sky, int mood, box_rgb_t color);

/** The local time glances show, for every glance: the box's own clock.
 *  Without one, a glance shows the time its sender gave. */
void box_scene_set_clock(box_clock_fn now, void *ctx);

/** Draw this scene in a fixed style rather than the display's. Only before
 *  the scene is shared. */
void box_scene_set_style(box_scene_t *scene, box_style_t style);
box_style_t box_scene_style(const box_scene_t *scene);

box_scene_t *box_scene_retain(box_scene_t *scene);
void box_scene_release(box_scene_t *scene);

box_content_t box_scene_content(const box_scene_t *scene);
/** "clear", "color", "text", "image", "clock" or "glance". */
const char *box_scene_content_name(const box_scene_t *scene);
/** The title of a text scene (a glance's date), else "". */
const char *box_scene_title(const box_scene_t *scene);
/** The text of a text scene, the time a clock last showed or a glance's
 *  time and weather; else "". */
const char *box_scene_text(const box_scene_t *scene);
/** The colour of a color, text or clock scene; black for the others. */
box_rgb_t box_scene_color(const box_scene_t *scene);

/** Changes exactly when the scene would draw differently at `t` seconds since
 *  it became active (an image's next frame, a clock's next minute). */
uint64_t box_scene_version(box_scene_t *scene, double t);

/** Draw at `t` seconds since the scene became active, in `style` (smooth or
 *  led; never default). For led, `frame` is the LED grid. */
void box_scene_render(box_scene_t *scene, box_frame_t *frame, double t, box_style_t style);

/** Draw again into `frame`, which still holds this scene's last render in
 *  `style`: a scene that moves may update only what moved, and narrow
 *  frame->changed to it. Others draw afresh. */
void box_scene_render_again(box_scene_t *scene, box_frame_t *frame, double t, box_style_t style);

/* Image memory budget. Image data lives as long as some queue entry, ambient
 * card or the engine holds it, so it is bounded as a whole rather than per
 * upload: a burst of large uploads must not exhaust the device. */
void box_images_set_budget(size_t bytes);
/** True when `bytes` more image data fits in the budget right now. */
bool box_images_budget_allows(size_t bytes);
size_t box_images_in_use(void);

const char *box_style_name(box_style_t style);
bool box_style_parse(const char *name, box_style_t *out);

#ifdef __cplusplus
}
#endif
