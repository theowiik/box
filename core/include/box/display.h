// Where frames go.
//
// The core decides what to show and draws it as a small RGB frame (96x96 by
// default). A display turns that frame into light: the ESP-Mosaico's screen
// (mosaico/), an LED matrix on a Raspberry Pi, a window, a test. Adding a
// target means implementing this, not touching the core.
#pragma once

#include <stdbool.h>

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** The screen size the core draws at, in pixels. */
    int width;
    int height;
    /** Show `frame` (width x height RGB). Called only when the frame changed,
     *  always from the same task, so a display may own its bus without
     *  locking. Returns true if new pixels went out. */
    bool (*show)(void *ctx, const box_frame_t *frame);
    /** Brightness 0..1, from the same task as show(). NULL if the display
     *  cannot dim. */
    bool (*set_brightness)(void *ctx, float brightness);
    void *ctx;
} box_display_t;

#ifdef __cplusplus
}
#endif
