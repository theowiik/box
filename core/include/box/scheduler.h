// Decides what the panel shows right now.
//
// Four tiers, in the order they win the panel:
//
//   alarm   — always takes the panel, interrupting even an alert.
//   alert   — takes the panel too, unless an instant event is already on it;
//             then it waits at the head of the queue and shows next.
//   normal  — queued behind any waiting alerts, shown in turn for its duration.
//   ambient — a rotation of background cards, shown whenever nothing is queued.
//
// The split between the two instant tiers keeps a burst readable: three pings
// in the same second line up instead of each shoving the last off screen.
// Each source owns one ambient card and updates it in place, and keeps at most
// one pending event, so ten pings never buy eighty seconds of panel time.
//
// Not thread-safe: the device serializes every call under one lock.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "box/engine.h"
#include "box/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An interrupted event is requeued only if this much of it is still to come;
 *  resuming a notification for half a second reads as a glitch. */
#define BOX_MIN_RESUME 1.5
/** Waiting events. The one-per-source rule keeps real queues far shorter. */
#define BOX_QUEUE_MAX 32
/** Sources with a card in the idle rotation. */
#define BOX_AMBIENT_MAX 16

typedef struct box_scheduler box_scheduler_t;

typedef enum {
    BOX_PLACED_QUEUED,  /**< Waits its turn (or shows now if the panel was idle). */
    BOX_PLACED_NEXT,    /**< An alert waiting for the instant event on screen. */
    BOX_PLACED_PREEMPT, /**< Took the panel immediately. */
    BOX_PLACED_AMBIENT, /**< Joined or refreshed the idle rotation. */
} box_placement_t;

typedef struct {
    char id[13];
    char source[BOX_SOURCE_MAX + 1];
    box_priority_t priority;
    box_placement_t placement;
    double duration;
    size_t queued;
} box_accepted_t;

typedef struct {
    /** The content type: "clear", "color", "text", "image" or "clock". */
    const char *content;
    /** The scene's own style ("smooth" or "led"), or "default". */
    const char *style;
    /** Empty when nothing owns the panel. */
    char source[BOX_SOURCE_MAX + 1];
    /** NULL when nothing owns the panel. */
    const char *priority;
    /** A text scene's title and text (a clock's time), else empty. */
    char title[BOX_TITLE_MAX_CHARS * 4 + 1];
    char text[BOX_TEXT_MAX_CHARS * 4 + 1];
    box_rgb_t color;
    size_t queued;
    size_t ambient_count;
    char ambient[BOX_AMBIENT_MAX][BOX_SOURCE_MAX + 1];
} box_panel_state_t;

typedef void (*box_log_fn)(void *ctx, const char *line);

/** The scheduler drives `engine`, which must outlive it. `id_seed` varies the
 *  event ids between boots. Returns NULL when out of memory. */
box_scheduler_t *box_scheduler_new(box_engine_t *engine, uint32_t id_seed);
void box_scheduler_free(box_scheduler_t *scheduler);

/** One line per accepted event, e.g. "preempt discord: @theo mentioned you". */
void box_scheduler_set_log(box_scheduler_t *scheduler, box_log_fn fn, void *ctx);

/** Accept an event and put it wherever its priority belongs. On success the
 *  scheduler takes the event's scene (the event is cleared). On failure the
 *  event is left untouched and `err` says why (503: queue or rotation full).
 *
 *  `focus` jumps the ambient rotation straight to this card: an interactive
 *  choice should feel immediate, a timed refresh should not hijack the panel. */
bool box_scheduler_submit(box_scheduler_t *scheduler, box_event_t *event, bool focus,
                          double now, box_accepted_t *out, box_error_t *err);

/** Expire the event on screen and advance the queue or the rotation. */
void box_scheduler_tick(box_scheduler_t *scheduler, double now);

/** Drop the queue and the event on screen, falling back to ambient. */
void box_scheduler_clear(box_scheduler_t *scheduler, double now);
/** Empty the idle rotation. */
void box_scheduler_clear_ambient(box_scheduler_t *scheduler, double now);
/** Remove one source's ambient card. Returns false if it had none. */
bool box_scheduler_remove_ambient(box_scheduler_t *scheduler, const char *source, double now);

/** What to show when nothing else is: no event on screen and no ambient
 *  cards (a clock, say). Retained; replaces the previous idle scene. */
void box_scheduler_set_idle(box_scheduler_t *scheduler, box_scene_t *scene, double now);

size_t box_scheduler_queue_depth(const box_scheduler_t *scheduler);
void box_scheduler_state(const box_scheduler_t *scheduler, box_panel_state_t *out);

#ifdef __cplusplus
}
#endif
