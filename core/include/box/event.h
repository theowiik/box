// The event contract: what a sender POSTs, and how it becomes a scene.
//
//   {"source": "discord", "priority": "alert", "duration": 10,
//    "content": {"type": "text", "title": "Discord", "text": "@theo mentioned you"}}
//
// `content` describes *what* to show, not pixels: the box lays text out,
// scales images to fit and picks colours, so senders do not need to know the
// screen. Content types:
//
//   text   {"text", "title"?, "color"?, "align"?: "center" | "left"}
//   image  {"width", "height", "data": base64 RGB frames, "fps"?, "loop"?,
//           "scaling"?: "pixelated" | "smooth"}
//   color  {"color"}
//   clear  {}
//
// An event may also fix its "style" ("smooth" or "led") instead of the
// display's. A sender that names no colour gets its source's brand colour,
// or its priority's, so restyling every notification is one edit here.
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
    /** Jumps the queue — but waits if another instant event is on screen. */
    BOX_PRIORITY_ALERT,
    /** Always takes the screen, interrupting even an alert. Use it sparingly. */
    BOX_PRIORITY_ALARM,
} box_priority_t;

#define BOX_SOURCE_MAX 64
#define BOX_TEXT_MAX_CHARS 512
#define BOX_TITLE_MAX_CHARS 80
#define BOX_DURATION_MAX 3600.0
#define BOX_IMAGE_MAX_SIDE 1024
/** Ceiling on one image upload, decoded. Small images are fine: the box
 *  scales them to the screen. */
#define BOX_IMAGE_MAX_BYTES (4u * 1024u * 1024u)

typedef struct {
    char source[BOX_SOURCE_MAX + 1];
    box_priority_t priority;
    /** Seconds on screen: the event's own, or the priority's default. */
    double duration;
    /** The accent colour actually used, reported in the status. */
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
 *  invalid event, 503 when image memory is full). */
bool box_event_parse(const char *json, size_t len, box_event_t *out, box_error_t *err);

/** box_event_parse(), freeing `json` (with free()) as soon as it is parsed:
 *  for a large upload, the raw body need not outlive the parsed document. */
bool box_event_parse_take(char *json, size_t len, box_event_t *out, box_error_t *err);

/** A text event built in code, for the box's own messages. `title` may be NULL. */
bool box_event_text(box_event_t *out, const char *source, const char *title, const char *text,
                    box_priority_t priority, double duration);

/** Release the event's scene, if it still holds one. */
void box_event_clear(box_event_t *event);

#ifdef __cplusplus
}
#endif
