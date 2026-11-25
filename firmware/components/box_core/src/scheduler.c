#include "box/scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How much of an event's text a log line carries. */
#define LOG_SUMMARY 80

typedef struct item {
    char id[13];
    box_event_t event; /* Owns the scene. */
    double duration;   /* Remaining time, for an event resumed after preemption. */
    double started_at;
    bool started;
    struct item *next;
} item_t;

typedef struct {
    char source[BOX_SOURCE_MAX + 1];
    box_scene_t *scene;
    double dwell;
    box_rgb_t color;
} card_t;

struct box_scheduler {
    box_engine_t *engine;
    box_scene_t *blank;

    item_t *queue; /* Highest rank first, FIFO within a rank. */
    size_t queue_len;
    item_t *current;

    card_t ambient[BOX_AMBIENT_MAX];
    size_t ambient_count;
    size_t ambient_index;
    double ambient_started;
    /* Which card is on screen, by source; empty when none is. */
    char ambient_showing[BOX_SOURCE_MAX + 1];

    uint32_t id_seed;
    uint32_t id_counter;
    box_log_fn log;
    void *log_ctx;
};

/* --- helpers ------------------------------------------------------------- */

static void item_free(item_t *item)
{
    if (item) {
        box_event_clear(&item->event);
        free(item);
    }
}

static bool item_expired(const item_t *item, double now)
{
    return item->started && now - item->started_at >= item->duration;
}

static double item_remaining(const item_t *item, double now)
{
    if (!item->started) {
        return item->duration;
    }
    const double left = item->duration - (now - item->started_at);
    return left > 0.0 ? left : 0.0;
}

static void card_clear(card_t *card)
{
    box_scene_release(card->scene);
    memset(card, 0, sizeof(*card));
}

static bool showing_ambient(const box_scheduler_t *s)
{
    return s->ambient_showing[0] != '\0';
}

static void set_showing(box_scheduler_t *s, const char *source)
{
    snprintf(s->ambient_showing, sizeof(s->ambient_showing), "%s", source ? source : "");
}

static const char *placement_name(box_placement_t placement)
{
    switch (placement) {
    case BOX_PLACED_NEXT:
        return "next";
    case BOX_PLACED_PREEMPT:
        return "preempt";
    case BOX_PLACED_AMBIENT:
        return "ambient";
    case BOX_PLACED_QUEUED:
    default:
        return "queued";
    }
}

static void log_event(box_scheduler_t *s, box_placement_t placement, const char *source,
                      box_priority_t priority, const box_scene_t *scene)
{
    if (!s->log) {
        return;
    }
    const char *text = box_scene_text(scene);
    char line[160];
    snprintf(line, sizeof(line), "%-7s %s (%s): %.*s", placement_name(placement), source,
             box_priority_name(priority), LOG_SUMMARY, text ? text : box_scene_kind_name(scene));
    s->log(s->log_ctx, line);
}

/* --- lifecycle ----------------------------------------------------------- */

box_scheduler_t *box_scheduler_new(box_engine_t *engine, uint32_t id_seed)
{
    box_scheduler_t *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->blank = box_scene_new_blank();
    if (!s->blank) {
        free(s);
        return NULL;
    }
    s->engine = engine;
    s->id_seed = id_seed;
    return s;
}

void box_scheduler_free(box_scheduler_t *s)
{
    if (!s) {
        return;
    }
    while (s->queue) {
        item_t *next = s->queue->next;
        item_free(s->queue);
        s->queue = next;
    }
    item_free(s->current);
    for (size_t i = 0; i < s->ambient_count; ++i) {
        card_clear(&s->ambient[i]);
    }
    box_scene_release(s->blank);
    free(s);
}

void box_scheduler_set_log(box_scheduler_t *s, box_log_fn fn, void *ctx)
{
    s->log = fn;
    s->log_ctx = ctx;
}

size_t box_scheduler_queue_depth(const box_scheduler_t *s)
{
    return s->queue_len;
}

/* --- the queue ----------------------------------------------------------- */

/* Insert by rank: alerts wait ahead of normals, FIFO within a tier. */
static void enqueue(box_scheduler_t *s, item_t *item)
{
    item_t **link = &s->queue;
    while (*link && (*link)->event.priority >= item->event.priority) {
        link = &(*link)->next;
    }
    item->next = *link;
    *link = item;
    ++s->queue_len;
}

static void push_front(box_scheduler_t *s, item_t *item)
{
    item->next = s->queue;
    s->queue = item;
    ++s->queue_len;
}

static item_t *pop_front(box_scheduler_t *s)
{
    item_t *item = s->queue;
    if (item) {
        s->queue = item->next;
        item->next = NULL;
        --s->queue_len;
    }
    return item;
}

static size_t pending_from(const box_scheduler_t *s, const char *source)
{
    size_t count = 0;
    for (const item_t *i = s->queue; i; i = i->next) {
        count += strcmp(i->event.source, source) == 0;
    }
    return count;
}

static void drop_pending_from(box_scheduler_t *s, const char *source)
{
    item_t **link = &s->queue;
    while (*link) {
        if (strcmp((*link)->event.source, source) == 0) {
            item_t *gone = *link;
            *link = gone->next;
            --s->queue_len;
            item_free(gone);
        } else {
            link = &(*link)->next;
        }
    }
}

static void show(box_scheduler_t *s, item_t *item, double now)
{
    item->started = true;
    item->started_at = now;
    s->current = item;
    set_showing(s, NULL);
    box_engine_set_scene(s->engine, item->event.scene, now);
}

static bool activate_next(box_scheduler_t *s, double now)
{
    item_t *next = pop_front(s);
    if (!next) {
        return false;
    }
    show(s, next, now);
    return true;
}

static void preempt(box_scheduler_t *s, item_t *item, double now)
{
    item_t *current = s->current;
    s->current = NULL;
    if (current && item_remaining(current, now) >= BOX_MIN_RESUME) {
        /* Back at the very head with what is left of it — ahead even of
         * same-rank items waiting, since it was halfway through being read. */
        current->duration = item_remaining(current, now);
        current->started = false;
        push_front(s, current);
    } else {
        item_free(current);
    }
    show(s, item, now);
}

static box_placement_t place(box_scheduler_t *s, item_t *item, double now)
{
    if (item->event.priority == BOX_PRIORITY_ALARM) {
        preempt(s, item, now);
        return BOX_PLACED_PREEMPT;
    }
    if (item->event.priority == BOX_PRIORITY_ALERT) {
        if (s->current && s->current->event.priority >= BOX_PRIORITY_ALERT) {
            /* Let the instant event on the panel be read; this one goes next,
             * ahead of everything ordinary that is waiting. */
            enqueue(s, item);
            return BOX_PLACED_NEXT;
        }
        preempt(s, item, now);
        return BOX_PLACED_PREEMPT;
    }
    enqueue(s, item);
    if (!s->current) {
        activate_next(s, now);
    }
    return BOX_PLACED_QUEUED;
}

/* --- the ambient rotation ------------------------------------------------ */

/* Put a specific card on screen and restart its dwell. */
static void focus_ambient(box_scheduler_t *s, size_t index, double now)
{
    const card_t *card = &s->ambient[index];
    s->ambient_index = index;
    set_showing(s, card->source);
    s->ambient_started = now;
    if (!s->current) {
        box_engine_set_scene(s->engine, card->scene, now);
    }
}

/* Show the current card, advancing when its dwell is up. */
static void show_ambient(box_scheduler_t *s, bool force, double now)
{
    if (s->ambient_count == 0) {
        if (force || showing_ambient(s)) {
            set_showing(s, NULL);
            box_engine_set_scene(s->engine, s->blank, now);
        }
        return;
    }
    s->ambient_index %= s->ambient_count;
    const card_t *card = &s->ambient[s->ambient_index];
    if (force || !showing_ambient(s)) {
        set_showing(s, card->source);
        s->ambient_started = now;
        box_engine_set_scene(s->engine, card->scene, now);
        return;
    }
    if (now - s->ambient_started >= card->dwell) {
        s->ambient_index = (s->ambient_index + 1) % s->ambient_count;
        card = &s->ambient[s->ambient_index];
        set_showing(s, card->source);
        s->ambient_started = now;
        box_engine_set_scene(s->engine, card->scene, now);
    }
}

static int find_card(const box_scheduler_t *s, const char *source)
{
    for (size_t i = 0; i < s->ambient_count; ++i) {
        if (strcmp(s->ambient[i].source, source) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Replace this source's card, or append a new one. Takes the event's scene. */
static void upsert_ambient(box_scheduler_t *s, box_event_t *event, bool focus, double now)
{
    card_t card = {0};
    snprintf(card.source, sizeof(card.source), "%s", event->source);
    card.scene = event->scene;
    card.dwell = event->duration;
    card.color = event->color;
    event->scene = NULL;

    const int existing = find_card(s, card.source);
    if (existing >= 0) {
        card_clear(&s->ambient[existing]);
        s->ambient[existing] = card;
        /* Refresh in place without disturbing where the rotation is. */
        if (focus || strcmp(s->ambient_showing, card.source) == 0) {
            focus_ambient(s, (size_t)existing, now);
        }
        return;
    }
    s->ambient[s->ambient_count++] = card;
    if (focus || (!s->current && s->ambient_count == 1)) {
        focus_ambient(s, s->ambient_count - 1, now);
    }
}

/* --- public -------------------------------------------------------------- */

static void make_id(box_scheduler_t *s, char out[13])
{
    /* Unique per boot and varied across boots; not a secret. */
    const uint64_t n = ((uint64_t)s->id_seed << 32 | ++s->id_counter) * 0x9E3779B97F4A7C15ull;
    snprintf(out, 13, "%012llx", (unsigned long long)(n >> 16) & 0xFFFFFFFFFFFFull);
}

bool box_scheduler_submit(box_scheduler_t *s, box_event_t *event, bool focus, double now,
                          box_accepted_t *out, box_error_t *err)
{
    memset(out, 0, sizeof(*out));
    if (!event->scene) {
        err->status = 422;
        snprintf(err->message, sizeof(err->message), "event has no scene");
        return false;
    }
    const box_placement_t would = event->priority == BOX_PRIORITY_AMBIENT
                                      ? BOX_PLACED_AMBIENT
                                      : BOX_PLACED_QUEUED;
    /* Check capacity before anything changes, so a refusal has no effect. */
    if (would == BOX_PLACED_AMBIENT) {
        if (find_card(s, event->source) < 0 && s->ambient_count >= BOX_AMBIENT_MAX) {
            err->status = 503;
            snprintf(err->message, sizeof(err->message),
                     "the ambient rotation is full (%d sources)", BOX_AMBIENT_MAX);
            return false;
        }
    } else if (s->queue_len - pending_from(s, event->source) >= BOX_QUEUE_MAX) {
        err->status = 503;
        snprintf(err->message, sizeof(err->message), "the queue is full (%d events)",
                 BOX_QUEUE_MAX);
        return false;
    }

    make_id(s, out->id);
    snprintf(out->source, sizeof(out->source), "%s", event->source);
    out->priority = event->priority;
    out->duration = event->duration;

    if (would == BOX_PLACED_AMBIENT) {
        log_event(s, BOX_PLACED_AMBIENT, event->source, event->priority, event->scene);
        upsert_ambient(s, event, focus, now);
        box_event_clear(event);
        out->placement = BOX_PLACED_AMBIENT;
        out->queued = s->queue_len;
        return true;
    }

    item_t *item = calloc(1, sizeof(*item));
    if (!item) {
        err->status = 503;
        snprintf(err->message, sizeof(err->message), "out of memory");
        return false;
    }
    memcpy(item->id, out->id, sizeof(item->id));
    item->event = *event;
    item->duration = event->duration;
    memset(event, 0, sizeof(*event)); /* The item owns the scene now. */

    /* One pending item per source: a burst of ten pings should not buy eighty
     * seconds of panel time. */
    drop_pending_from(s, item->event.source);
    out->placement = place(s, item, now);
    log_event(s, out->placement, item->event.source, item->event.priority, item->event.scene);
    out->queued = s->queue_len;
    return true;
}

void box_scheduler_tick(box_scheduler_t *s, double now)
{
    if (s->current) {
        if (!item_expired(s->current, now)) {
            return;
        }
        item_free(s->current);
        s->current = NULL;
        if (activate_next(s, now)) {
            return;
        }
        show_ambient(s, true, now);
        return;
    }
    show_ambient(s, false, now);
}

void box_scheduler_clear(box_scheduler_t *s, double now)
{
    while (s->queue) {
        item_free(pop_front(s));
    }
    item_free(s->current);
    s->current = NULL;
    show_ambient(s, true, now);
}

void box_scheduler_clear_ambient(box_scheduler_t *s, double now)
{
    for (size_t i = 0; i < s->ambient_count; ++i) {
        card_clear(&s->ambient[i]);
    }
    s->ambient_count = 0;
    s->ambient_index = 0;
    set_showing(s, NULL);
    if (!s->current) {
        box_engine_set_scene(s->engine, s->blank, now);
    }
}

bool box_scheduler_remove_ambient(box_scheduler_t *s, const char *source, double now)
{
    const int index = find_card(s, source);
    if (index < 0) {
        return false;
    }
    const bool was_showing = strcmp(s->ambient_showing, source) == 0;
    card_clear(&s->ambient[index]);
    memmove(&s->ambient[index], &s->ambient[index + 1],
            (s->ambient_count - (size_t)index - 1) * sizeof(card_t));
    --s->ambient_count;
    memset(&s->ambient[s->ambient_count], 0, sizeof(card_t));
    if ((size_t)index < s->ambient_index) {
        --s->ambient_index;
    }
    if (was_showing) {
        set_showing(s, NULL);
        if (!s->current) {
            show_ambient(s, true, now);
        }
    }
    return true;
}

void box_scheduler_state(const box_scheduler_t *s, box_panel_state_t *out)
{
    memset(out, 0, sizeof(*out));
    const box_scene_t *scene = NULL;
    if (s->current) {
        scene = s->current->event.scene;
        snprintf(out->source, sizeof(out->source), "%s", s->current->event.source);
        out->priority = box_priority_name(s->current->event.priority);
        out->color = s->current->event.color;
    } else if (showing_ambient(s)) {
        const int index = find_card(s, s->ambient_showing);
        if (index >= 0) {
            scene = s->ambient[index].scene;
            out->color = s->ambient[index].color;
        }
        snprintf(out->source, sizeof(out->source), "%s", s->ambient_showing);
        out->priority = box_priority_name(BOX_PRIORITY_AMBIENT);
    }
    if (!scene) {
        box_rgb_parse("#7A8A99", &out->color);
    }
    out->scene = scene ? box_scene_kind_name(scene) : "blank";
    const char *text = scene ? box_scene_text(scene) : NULL;
    if (text) {
        snprintf(out->text, sizeof(out->text), "%s", text);
    }
    out->queued = s->queue_len;
    out->ambient_count = s->ambient_count;
    for (size_t i = 0; i < s->ambient_count; ++i) {
        memcpy(out->ambient[i], s->ambient[i].source, sizeof(out->ambient[i]));
    }
}
