#include "box/box.h"

#include <stdlib.h>

#include "box/engine.h"

#define NO_BRIGHTNESS (-1.0f)

struct box {
    box_display_t display;
    box_engine_t engine;
    box_scheduler_t *scheduler;
    float brightness;
    /* Requested under the lock... */
    float pending_brightness;
    /* ...handed to the render loop by box_render(), applied by box_show(). */
    float apply_brightness;
};

box_t *box_new(const box_display_t *display, int grid, uint32_t seed)
{
    box_t *box = calloc(1, sizeof(*box));
    if (!box) {
        return NULL;
    }
    box->display = *display;
    box->brightness = 1.0f;
    box->pending_brightness = NO_BRIGHTNESS;
    box->apply_brightness = NO_BRIGHTNESS;
    if (!box_engine_init(&box->engine, display->width, display->height, grid)) {
        free(box);
        return NULL;
    }
    box->scheduler = box_scheduler_new(&box->engine, seed);
    if (!box->scheduler) {
        box_engine_free(&box->engine);
        free(box);
        return NULL;
    }
    return box;
}

void box_free(box_t *box)
{
    if (!box) {
        return;
    }
    box_scheduler_free(box->scheduler);
    box_engine_free(&box->engine);
    free(box);
}

box_scheduler_t *box_scheduler(box_t *box)
{
    return box->scheduler;
}

void box_set_idle(box_t *box, box_scene_t *scene, double now)
{
    box_scheduler_set_idle(box->scheduler, scene, now);
}

void box_request_brightness(box_t *box, float brightness)
{
    box->brightness = brightness;
    box->pending_brightness = brightness;
}

float box_brightness(const box_t *box)
{
    return box->brightness;
}

void box_set_style(box_t *box, box_style_t style)
{
    box_engine_set_style(&box->engine, style);
}

box_style_t box_style(const box_t *box)
{
    return box->engine.style;
}

int box_grid(const box_t *box)
{
    return box->engine.grid.width;
}

const box_frame_t *box_render(box_t *box, double now)
{
    if (box->pending_brightness != NO_BRIGHTNESS) {
        box->apply_brightness = box->pending_brightness;
        box->pending_brightness = NO_BRIGHTNESS;
    }
    box_scheduler_tick(box->scheduler, now);
    return box_engine_render(&box->engine, now);
}

bool box_show(box_t *box, const box_frame_t *frame)
{
    if (box->apply_brightness != NO_BRIGHTNESS) {
        if (box->display.set_brightness) {
            box->display.set_brightness(box->display.ctx, box->apply_brightness);
        }
        box->apply_brightness = NO_BRIGHTNESS;
    }
    return frame ? box->display.show(box->display.ctx, frame) : false;
}
