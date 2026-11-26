// The led style: a coarse grid of square LEDs filling the screen.
#pragma once

#include "box/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Draw `grid` onto `screen` with each grid pixel as a square block, the
 *  grid centred and as large as whole blocks allow; the rest is black. */
void box_led_render(const box_frame_t *grid, box_frame_t *screen);

#ifdef __cplusplus
}
#endif
