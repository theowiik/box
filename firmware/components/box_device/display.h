// The CO5300 round display, driven through esp_display_present.
//
// Private to box_device: the runtime renders the LED look into a full-screen
// RGB565 image and hands the changed area here.
#pragma once

#include <stdint.h>

#include "box/led.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** Initialize the panel and its presenter. Fills the screen size. */
esp_err_t box_display_start(int *out_width, int *out_height);

/** Push the `dirty` area of `screen` (width * height RGB565, native byte order). */
esp_err_t box_display_present(const uint16_t *screen, const box_rect_t *dirty);

/** Panel brightness in percent, 0..100. Brightness is a panel command on the
 *  same bus as the pixels, so call this only from the task that presents
 *  frames (or before it starts): it waits for in-flight transfers first. */
esp_err_t box_display_set_brightness(int percent);

/** Frames whose transfer to the panel has completed since start. */
uint64_t box_display_frames_shown(void);

/** Serve `iris screenshot` from `screen`, copied under `lock`. */
esp_err_t box_display_register_screenshot(const uint16_t *screen, SemaphoreHandle_t lock);
