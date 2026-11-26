// The HTTP API's JSON, shared by every target's web server.
//
//   POST   /api/events    an event (box/event.h)            -> box_api_accepted
//   DELETE /api/events    drop the queue and what is showing -> box_api_ok
//   DELETE /api/ambient   empty the idle rotation           -> box_api_ok
//   GET    /api/status    what is on screen                 -> box_api_status
//   GET    /api/display   what the display can do           -> box_api_display
//   PATCH  /api/display   {"brightness"?: 0..1, "style"?}   -> box_api_parse_settings
//
// Errors are {"detail": "..."} with the status from box_error_t.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "box/scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the platform knows beyond the scheduler's state. */
typedef struct {
    int width;  /**< the screen, in pixels */
    int height;
    const char *shape; /**< "square", "round", ... */
    int grid;          /**< the led style's grid, in LEDs per side */
    int fps;
    const char *style; /**< the display's style */
    float brightness;
    /** Measured over the last few seconds: frames drawn, and frames the
     *  hardware received (0 while nothing moves). */
    float drawn_fps;
    float shown_fps;
} box_api_info_t;

/* Each returns a NUL-terminated JSON string to release with box_api_free(),
 * or NULL when out of memory. */
char *box_api_status(const box_panel_state_t *state, const box_api_info_t *info);
char *box_api_display(const box_api_info_t *info);
char *box_api_accepted(const box_accepted_t *accepted);
char *box_api_error(const char *message);
char *box_api_ok(void);
void box_api_free(char *json);

/** Parse PATCH /api/display: {"brightness"?: 0..1, "style"?: "smooth"|"led"}.
 *  Fields left out keep their value (*has_* false). False with a 422-worthy
 *  message for anything invalid. */
bool box_api_parse_settings(const char *json, size_t len, bool *has_brightness,
                            float *brightness, bool *has_style, box_style_t *style,
                            box_error_t *err);

#ifdef __cplusplus
}
#endif
