#include "box/engine.h"

#include <string.h>

bool box_engine_init(box_engine_t *engine, int width, int height)
{
    memset(engine, 0, sizeof(*engine));
    if (!box_frame_init(&engine->frame, width, height) ||
        !box_frame_init(&engine->prev_frame, width, height)) {
        box_engine_free(engine);
        return false;
    }
    engine->scene = box_scene_new_blank();
    if (!engine->scene) {
        box_engine_free(engine);
        return false;
    }
    return true;
}

void box_engine_free(box_engine_t *engine)
{
    box_scene_release(engine->scene);
    box_scene_release(engine->prev);
    box_frame_free(&engine->frame);
    box_frame_free(&engine->prev_frame);
    memset(engine, 0, sizeof(*engine));
}

void box_engine_set_scene(box_engine_t *engine, box_scene_t *scene, double now)
{
    box_scene_retain(scene);
    /* A fade already in progress is cut: the outgoing scene becomes the new
     * starting point, so rapid switches never stack. */
    box_scene_release(engine->prev);
    engine->prev = engine->scene;
    engine->prev_started = engine->scene_started;
    engine->transition_started = now;
    engine->scene = scene;
    engine->scene_started = now;
}

/* v * a / 255, rounded: blending each side separately never carries past 255,
 * because the two weights always sum to 255. */
static inline uint8_t scale(uint8_t v, unsigned a)
{
    return (uint8_t)((v * a + 127) / 255);
}

const box_frame_t *box_engine_render(box_engine_t *engine, double now)
{
    box_scene_render(engine->scene, &engine->frame, now - engine->scene_started);
    if (!engine->prev) {
        return &engine->frame;
    }
    const double progress = (now - engine->transition_started) / BOX_TRANSITION;
    if (progress >= 1.0) {
        box_scene_release(engine->prev);
        engine->prev = NULL;
        return &engine->frame;
    }
    box_scene_render(engine->prev, &engine->prev_frame, now - engine->prev_started);
    const unsigned alpha = progress > 0.0 ? (unsigned)(progress * 255) : 0;
    uint8_t *cur = engine->frame.pixels;
    const uint8_t *old = engine->prev_frame.pixels;
    const size_t bytes = box_frame_bytes(&engine->frame);
    for (size_t i = 0; i < bytes; ++i) {
        cur[i] = (uint8_t)(scale(cur[i], alpha) + scale(old[i], 255 - alpha));
    }
    return &engine->frame;
}
