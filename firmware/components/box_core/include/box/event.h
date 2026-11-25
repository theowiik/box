// The event contract: what a connector sends, and how it becomes a scene.
//
// This is the box's stable surface. Every connector — a weather service on a
// laptop, a GIF sender, a one-line curl — POSTs this JSON and nothing else:
//
//   {"source": "discord", "priority": "alert", "duration": 10,
//    "scene": {"kind": "text", "text": "@theo mentioned you"}}
//
// Presentation lives here, not in the connector. A connector says *what
// happened*; the box picks the colour from the source (or the priority), so
// restyling every notification is one edit here.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "box/frame.h"
#include "box/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /** Background content. Joins the idle rotation, never interrupts. */
    BOX_PRIORITY_AMBIENT = 0,
    /** Shown in turn for its duration, then the panel returns to ambient. */
    BOX_PRIORITY_NORMAL,
    /** Jumps the queue — but waits if another instant event is on the panel. */
    BOX_PRIORITY_ALERT,
    /** Always takes the panel, interrupting even an alert. Use it sparingly. */
    BOX_PRIORITY_ALARM,
} box_priority_t;

#define BOX_SOURCE_MAX 64
#define BOX_TEXT_MAX_CHARS 512
#define BOX_DURATION_MAX 3600.0
#define BOX_FRAMES_MAX_SIDE 256
/** Ceiling on one animation upload, decoded: about 150 frames at 96x96, or
 *  1360 at 32x32 (smaller frames are scaled up to the panel). */
#define BOX_FRAMES_MAX_BYTES (4u * 1024u * 1024u)

typedef struct {
    char source[BOX_SOURCE_MAX + 1];
    box_priority_t priority;
    /** Seconds on the panel: the event's own, or the priority's default. */
    double duration;
    /** The colour actually on screen, reported in the panel state. */
    box_rgb_t color;
    /** Owned reference; NULL once moved into the scheduler. */
    box_scene_t *scene;
} box_event_t;

/** Why an event was refused, as an HTTP status and a readable message. */
typedef struct {
    int status;
    char message[192];
} box_error_t;

const char *box_priority_name(box_priority_t priority);
bool box_priority_parse(const char *name, box_priority_t *out);
double box_priority_default_duration(box_priority_t priority);
/** The colour for an event that named none: its source's brand, else its priority's. */
box_rgb_t box_source_color(const char *source, box_priority_t priority);

/** Parse and validate one event. On success `out` owns a scene; on failure
 *  `out` is cleared and `err` says why (400 for malformed JSON, 422 for an
 *  invalid event, 503 when animation memory is full). */
bool box_event_parse(const char *json, size_t len, box_event_t *out, box_error_t *err);

/** box_event_parse(), freeing `json` (with free()) as soon as it is parsed:
 *  for a large upload, the raw body need not outlive the parsed document. */
bool box_event_parse_take(char *json, size_t len, box_event_t *out, box_error_t *err);

/** A text event built in code, for the box's own status messages. */
bool box_event_text(box_event_t *out, const char *source, const char *text,
                    box_priority_t priority, double duration);

/** Release the event's scene, if it still holds one. */
void box_event_clear(box_event_t *event);

#ifdef __cplusplus
}
#endif
