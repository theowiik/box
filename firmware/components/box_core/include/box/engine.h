// The render engine: draws the active scene into the logical frame,
// crossfading from the previous one when the scene changes.
//
// Time is always passed in (seconds on any monotonic clock), so the engine
// and the scheduler are deterministic under test.
#pragma once

#include <stdbool.h>

#include "box/frame.h"
#include "box/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Crossfade duration when switching scenes, in seconds. */
#define BOX_TRANSITION 0.35

typedef struct {
    box_frame_t frame;
    box_frame_t prev_frame;
    box_scene_t *scene;
    box_scene_t *prev;
    double scene_started;
    double prev_started;
    double transition_started;
} box_engine_t;

/** Allocate both frames and start on a blank scene. Returns false on OOM. */
bool box_engine_init(box_engine_t *engine, int width, int height);
void box_engine_free(box_engine_t *engine);

/** Switch scenes (retaining `scene`), crossfading from the current one. */
void box_engine_set_scene(box_engine_t *engine, box_scene_t *scene, double now);

/** Render the frame for `now`; the result stays valid until the next call. */
const box_frame_t *box_engine_render(box_engine_t *engine, double now);

#ifdef __cplusplus
}
#endif
