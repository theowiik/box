// The render engine: draws the active scene at the screen's resolution,
// crossfading from what was on screen when the scene changes.
//
// A scene is re-rendered only when its version or the style changes, and
// box_engine_render() returns NULL when the frame is unchanged, so a still
// screen costs nothing. Time is passed in, so this is deterministic in tests.
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
    box_frame_t out;   /* the blend during a crossfade */
    box_frame_t scene; /* the current scene, as last rendered */
    box_frame_t prev;  /* what was on screen when the scene changed */
    box_frame_t grid;  /* the led style's coarse grid */
    box_style_t style; /* the display's style */
    box_scene_t *current;
    double started;
    bool rendered;
    uint64_t version;
    box_style_t rendered_style;
    const box_frame_t *shown;
    bool fading;
    double fade_started;
} box_engine_t;

/** A width x height screen, with a `grid` x `grid` LED grid for the led
 *  style. Starts on a clear scene, in the smooth style. False on OOM. */
bool box_engine_init(box_engine_t *engine, int width, int height, int grid);
void box_engine_free(box_engine_t *engine);

/** Switch scenes (retaining `scene`), crossfading from what is on screen. */
void box_engine_set_scene(box_engine_t *engine, box_scene_t *scene, double now);
/** The display's style (smooth or led), for scenes that do not fix their own. */
void box_engine_set_style(box_engine_t *engine, box_style_t style);

/** The frame for `now`, or NULL if it is the same as the last one returned.
 *  The frame stays valid until the next call. */
const box_frame_t *box_engine_render(box_engine_t *engine, double now);

#ifdef __cplusplus
}
#endif
