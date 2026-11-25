// The LED look: turns the small logical frame into round dots on the screen.
//
// Each logical pixel becomes one antialiased dot on a square grid centred on
// the display (filling it when the sizes divide evenly: 96 dots x 5 px = 480),
// with a faint glow for unlit dots, like a real LED panel. Only
// cells whose colour changed are redrawn, and the changed screen area is
// reported so the display pushes just that region.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Dot diameter as a share of the grid pitch. */
#define BOX_LED_DOT 0.8f
/** Unlit dots glow at this level (0-255), like a dark LED. */
#define BOX_LED_OFF 18

typedef struct {
    int x1, y1, x2, y2; /**< Inclusive screen coordinates. */
} box_rect_t;

typedef struct box_led box_led_t;

/** A renderer for a panel_w x panel_h logical frame on a screen_w x screen_h
 *  display. NULL on OOM or when the panel does not fit (pitch under 2 pixels). */
box_led_t *box_led_new(int panel_w, int panel_h, int screen_w, int screen_h);
void box_led_free(box_led_t *led);

/** The screen image: screen_w * screen_h RGB565 values in native byte order. */
const uint16_t *box_led_pixels(const box_led_t *led);
int box_led_pitch(const box_led_t *led);

/** Redraw the cells that changed since the last call (all of them the first
 *  time). Returns true and the changed area when anything did. */
bool box_led_update(box_led_t *led, const box_frame_t *frame, box_rect_t *dirty);

#ifdef __cplusplus
}
#endif
