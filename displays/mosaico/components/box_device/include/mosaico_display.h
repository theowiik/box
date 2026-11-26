// The ESP-Mosaico's 480x480 screen as a box display (box/display.h).
//
// The core's full-resolution frames go to the CO5300 AMOLED through
// esp_display_present with tear sync; only the rectangle that changed is
// sent. `iris screenshot` is served from the same image.
#pragma once

#include <stdint.h>

#include "box/display.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the panel and fill `out` with the screen's size and callbacks.
 *  Sets the starting brightness (0..1) before any frame goes out. */
esp_err_t mosaico_display_start(float brightness, box_display_t *out);

/** Frames whose transfer to the panel has completed since start. */
uint64_t mosaico_display_frames_shown(void);

#ifdef __cplusplus
}
#endif
