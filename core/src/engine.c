#include "box/engine.h"

#include <string.h>

#include "box/led.h"

bool box_engine_init(box_engine_t *engine, int width, int height, int grid)
{
    memset(engine, 0, sizeof(*engine));
    if (!box_frame_init(&engine->out, width, height) ||
        !box_frame_init(&engine->scene, width, height) ||
        !box_frame_init(&engine->prev, width, height) ||
        !box_frame_init(&engine->grid, grid, grid)) {
        box_engine_free(engine);
        return false;
    }
    engine->current = box_scene_new_clear();
    if (!engine->current) {
        box_engine_free(engine);
        return false;
    }
    engine->style = BOX_STYLE_SMOOTH;
    engine->shown = &engine->out; /* black */
    return true;
}

void box_engine_free(box_engine_t *engine)
{
    box_scene_release(engine->current);
    box_frame_free(&engine->out);
    box_frame_free(&engine->scene);
    box_frame_free(&engine->prev);
    box_frame_free(&engine->grid);
    memset(engine, 0, sizeof(*engine));
}

void box_engine_set_scene(box_engine_t *engine, box_scene_t *scene, double now)
{
    box_scene_retain(scene);
    box_scene_release(engine->current);
    engine->current = scene;
    engine->started = now;
    engine->rendered = false;
    /* Fade from whatever is on screen, mid-fade included. */
    if (engine->shown != &engine->prev) {
        memcpy(engine->prev.pixels, engine->shown->pixels, box_frame_bytes(&engine->prev));
    }
    engine->shown = &engine->prev;
    engine->fading = true;
    engine->fade_started = now;
}

void box_engine_set_style(box_engine_t *engine, box_style_t style)
{
    if (style != BOX_STYLE_DEFAULT) {
        engine->style = style;
    }
}

/* v * a / 255, rounded; the two sides' weights always sum to 255. */
static inline uint8_t scale(uint8_t v, unsigned a)
{
    return (uint8_t)((v * a + 127) / 255);
}

const box_frame_t *box_engine_render(box_engine_t *engine, double now)
{
    box_scene_t *scene = engine->current;
    const double t = now - engine->started;
    const box_style_t fixed = box_scene_style(scene);
    const box_style_t style = fixed != BOX_STYLE_DEFAULT ? fixed : engine->style;
    const uint64_t version = box_scene_version(scene, t);
    const bool redraw = !engine->rendered || version != engine->version ||
                        style != engine->rendered_style;
    if (redraw) {
        if (style == BOX_STYLE_LED) {
            box_scene_render(scene, &engine->grid, t, BOX_STYLE_LED);
            box_led_render(&engine->grid, &engine->scene);
        } else {
            box_scene_render(scene, &engine->scene, t, BOX_STYLE_SMOOTH);
        }
        engine->rendered = true;
        engine->version = version;
        engine->rendered_style = style;
    }
    if (engine->fading) {
        const double progress = (now - engine->fade_started) / BOX_TRANSITION;
        if (progress >= 1.0) {
            engine->fading = false;
            engine->shown = &engine->scene;
            return engine->shown;
        }
        const unsigned alpha = progress > 0.0 ? (unsigned)(progress * 255) : 0;
        const uint8_t *cur = engine->scene.pixels, *old = engine->prev.pixels;
        uint8_t *out = engine->out.pixels;
        const size_t bytes = box_frame_bytes(&engine->out);
        for (size_t i = 0; i < bytes; ++i) {
            out[i] = (uint8_t)(scale(cur[i], alpha) + scale(old[i], 255 - alpha));
        }
        engine->shown = &engine->out;
        return engine->shown;
    }
    if (redraw) {
        engine->shown = &engine->scene;
        return engine->shown;
    }
    return NULL;
}
