// The glance's drawing, for scene.c.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "box/frame.h"

typedef enum {
    BOX_SKY_SUN,
    BOX_SKY_MOON,
    BOX_SKY_PARTLY,
    BOX_SKY_CLOUD,
    BOX_SKY_FOG,
    BOX_SKY_RAIN,
    BOX_SKY_SNOW,
    BOX_SKY_STORM,
} box_glance_sky_t;

typedef enum {
    BOX_MOOD_HAPPY,
    BOX_MOOD_GOOD,
    BOX_MOOD_MEH,
    BOX_MOOD_SAD,
} box_glance_mood_t;

/** The names the API takes, NULL-terminated, in enum order. */
extern const char *const BOX_GLANCE_SKIES[];
extern const char *const BOX_GLANCE_MOODS[];

typedef struct {
    const uint32_t *cps;
    size_t count;
} box_glance_text_t;

typedef struct {
    box_glance_text_t date, time, temp, condition;
    box_glance_sky_t sky;
    box_glance_mood_t mood;
    box_rgb_t color;
} box_glance_t;

void box_glance_draw_smooth(box_frame_t *frame, const box_glance_t *glance);
void box_glance_draw_led(box_frame_t *frame, const box_glance_t *glance);
