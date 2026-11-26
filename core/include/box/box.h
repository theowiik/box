// The whole box, minus I/O and threads.
//
// A target supplies a display (box/display.h), a clock, a lock and a render
// loop, and feeds events in from whatever network it has. Everything about
// *what* is on the panel lives here:
//
//   HTTP / any input ──► box_submit ─┐
//                                    ├─(platform lock)─► scheduler, engine
//   render loop: box_render(now) ────┘                        │
//                box_show(frame) ──────────────────────► display
//
// Locking: every function except box_show() touches shared state; the
// platform serializes them (one mutex). box_show() runs on the render loop
// only, outside the lock, so a slow display never blocks incoming events.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "box/display.h"
#include "box/scene.h"
#include "box/scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct box box_t;

/** A box drawing at the display's size, with a `grid` x `grid` LED grid for
 *  the led style. `seed` varies event ids per boot. The display must outlive
 *  the box. NULL when out of memory. */
box_t *box_new(const box_display_t *display, int grid, uint32_t seed);
void box_free(box_t *box);

/** The scheduler, for submitting events and reading state (under the lock). */
box_scheduler_t *box_scheduler(box_t *box);

/** What the panel shows when there is nothing else: no event on screen and
 *  no ambient cards. Retained. Defaults to clear. */
void box_set_idle(box_t *box, box_scene_t *scene, double now);

/** Ask for a brightness (0..1); the render loop applies it before the next
 *  frame, since a display's bus belongs to that loop. */
void box_request_brightness(box_t *box, float brightness);
float box_brightness(const box_t *box);

/** The style for scenes that do not fix their own (smooth or led). */
void box_set_style(box_t *box, box_style_t style);
box_style_t box_style(const box_t *box);
int box_grid(const box_t *box);

/** Advance to `now` and draw the frame (under the lock), or NULL when the
 *  screen is unchanged. The frame stays valid until the next box_render(). */
const box_frame_t *box_render(box_t *box, double now);

/** Apply any pending brightness, then show the frame from box_render().
 *  Render loop only, outside the lock. Returns true if new pixels went out. */
bool box_show(box_t *box, const box_frame_t *frame);

#ifdef __cplusplus
}
#endif
