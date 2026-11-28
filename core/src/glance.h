// The glance's drawing, for scene.c.
#pragma once

#include <stdbool.h>
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

/** The moving parts: the dot clock and the smiley. One per scene. */
typedef struct box_glance_anim box_glance_anim_t;
box_glance_anim_t *box_glance_anim_new(void);
void box_glance_anim_free(box_glance_anim_t *anim);
/** Points the dots at `time` (HH:MM) and `second` (0-59, or -1 when unknown:
 *  no seconds), and changes exactly when the glance would draw differently
 *  at `t`: every frame while dots fly or the smiley moves. */
uint64_t box_glance_anim_version(box_glance_anim_t *anim, const uint32_t *time, size_t count,
                                 int second, double t);

void box_glance_draw_smooth(box_frame_t *frame, const box_glance_t *glance,
                            box_glance_anim_t *anim, double t);
/** Update a frame that holds the last smooth draw: only what moved. */
void box_glance_draw_smooth_again(box_frame_t *frame, const box_glance_t *glance,
                                  box_glance_anim_t *anim, double t);
void box_glance_draw_led(box_frame_t *frame, const box_glance_t *glance,
                         const box_glance_anim_t *anim, double t);
