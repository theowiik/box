// The running box: owns the core objects and the render task, and is the
// only way other tasks (HTTP, Wi-Fi events) reach them.
//
// Every function here is thread-safe: the scheduler and render engine are
// single-threaded, so each call takes the runtime's lock for its duration.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "box/event.h"
#include "box/scheduler.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;  /**< Logical panel, in LED dots. */
    int height;
    int fps;
    float brightness; /**< 0..1, the panel's backlight level. */
    /** Measured over the last few seconds (0 while nothing moves): frames
     *  drawn that changed the screen, and frames the panel received. */
    float drawn_fps;
    float shown_fps;
} box_runtime_info_t;

/** Bring up the display and start rendering. Call once, from app_main. */
esp_err_t box_runtime_start(float brightness);

/** Hand an event to the scheduler. On success the event is consumed. */
bool box_runtime_submit(box_event_t *event, bool focus, box_accepted_t *out, box_error_t *err);

/** Show a text status card from the box itself (source "box"), or remove it
 *  with text NULL. Ambient, so it never interrupts anything sent to the box. */
void box_runtime_status(const char *text);

/** Announce something from the box itself as a normal event. */
void box_runtime_announce(const char *text, double duration);

void box_runtime_clear_queue(void);
/** Drop the queue and empty the idle rotation. */
void box_runtime_clear_all(void);

/** Copy what the panel shows; `out` is large, so allocate it on the heap. */
void box_runtime_state(box_panel_state_t *out);
void box_runtime_info(box_runtime_info_t *out);

/** 0..1. Applied by the render task within a frame; not persisted across
 *  reboots (the Vibe Mode preference is the boot value). */
esp_err_t box_runtime_set_brightness(float brightness);

#ifdef __cplusplus
}
#endif
