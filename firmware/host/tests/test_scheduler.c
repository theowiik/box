// Priorities, coalescing, preemption and the ambient rotation. Ported from the
// Python box's tests/test_scheduler.py, plus the device's capacity limits.
#include <stdlib.h>

#include "box/scheduler.h"
#include "check.h"

typedef struct {
    box_engine_t engine;
    box_scheduler_t *s;
    double now;
} fixture_t;

static void setup(fixture_t *f)
{
    box_engine_init(&f->engine, 32, 32);
    f->s = box_scheduler_new(&f->engine, 1);
    f->now = 100.0;
}

static void teardown(fixture_t *f)
{
    box_scheduler_free(f->s);
    box_engine_free(&f->engine);
}

static box_accepted_t submit_full(fixture_t *f, const char *source, const char *text,
                                  box_priority_t priority, double duration, bool focus)
{
    box_event_t ev;
    box_event_text(&ev, source, text, priority, duration);
    box_accepted_t acc;
    box_error_t err;
    const bool ok = box_scheduler_submit(f->s, &ev, focus, f->now, &acc, &err);
    CHECK(ok);
    CHECK(ev.scene == NULL); /* the scheduler took it */
    if (!ok) {
        box_event_clear(&ev);
    }
    return acc;
}

static box_accepted_t submit(fixture_t *f, const char *source, const char *text,
                             box_priority_t priority, double duration)
{
    return submit_full(f, source, text, priority, duration, false);
}

/* What the engine is drawing now: the text, or the scene kind. */
static const char *on_screen(const fixture_t *f)
{
    const char *text = box_scene_text(f->engine.scene);
    return text ? text : box_scene_kind_name(f->engine.scene);
}

static void advance(fixture_t *f, double seconds)
{
    /* Tick at 10 Hz like the old Python loop; the device ticks every frame. */
    const double end = f->now + seconds;
    while (f->now < end - 1e-9) {
        f->now += 0.1;
        box_scheduler_tick(f->s, f->now);
    }
}

static void test_ambient_shows_when_nothing_queued(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    CHECK_STR(on_screen(&f), "SUNNY");
    teardown(&f);
}

static void test_normal_takes_the_panel_then_returns_to_ambient(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    submit(&f, "github", "PR MERGED", BOX_PRIORITY_NORMAL, 5);
    CHECK_STR(on_screen(&f), "PR MERGED");
    advance(&f, 5.05);
    CHECK_STR(on_screen(&f), "SUNNY");
    teardown(&f);
}

static void test_events_queue_in_order(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "a", "ONE", BOX_PRIORITY_NORMAL, 2);
    const box_accepted_t acc = submit(&f, "b", "TWO", BOX_PRIORITY_NORMAL, 2);
    submit(&f, "c", "THREE", BOX_PRIORITY_NORMAL, 2);
    CHECK(acc.placement == BOX_PLACED_QUEUED && acc.queued == 1);
    CHECK_STR(on_screen(&f), "ONE");
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "TWO");
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "THREE");
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "blank");
    teardown(&f);
}

static void test_burst_from_one_source_is_coalesced(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "other", "FIRST", BOX_PRIORITY_NORMAL, 2);
    for (int i = 0; i < 10; ++i) {
        submit(&f, "discord", i == 9 ? "PING 10" : "PING", BOX_PRIORITY_NORMAL, 2);
    }
    CHECK(box_scheduler_queue_depth(f.s) == 1);
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "PING 10");
    teardown(&f);
}

static void test_alert_preempts_and_the_preempted_event_resumes(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "github", "LONG READ", BOX_PRIORITY_NORMAL, 10);
    advance(&f, 3.0);
    const box_accepted_t acc = submit(&f, "discord", "PING", BOX_PRIORITY_ALERT, 2);
    CHECK(acc.placement == BOX_PLACED_PREEMPT);
    CHECK_STR(on_screen(&f), "PING");
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "LONG READ");
    /* About 7 of its 10 seconds were left. */
    advance(&f, 6.8);
    CHECK_STR(on_screen(&f), "LONG READ");
    advance(&f, 0.4);
    CHECK_STR(on_screen(&f), "blank");
    teardown(&f);
}

static void test_alert_waits_for_an_alert_already_on_the_panel(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "discord", "PING A", BOX_PRIORITY_ALERT, 3);
    const box_accepted_t acc = submit(&f, "slack", "PING B", BOX_PRIORITY_ALERT, 3);
    CHECK(acc.placement == BOX_PLACED_NEXT);
    CHECK_STR(on_screen(&f), "PING A");
    advance(&f, 3.05);
    CHECK_STR(on_screen(&f), "PING B");
    teardown(&f);
}

static void test_a_waiting_alert_shows_before_queued_normals(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "discord", "ALERT A", BOX_PRIORITY_ALERT, 2);
    submit(&f, "github", "NORMAL", BOX_PRIORITY_NORMAL, 2);
    submit(&f, "slack", "ALERT B", BOX_PRIORITY_ALERT, 2);
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "ALERT B");
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "NORMAL");
    teardown(&f);
}

static void test_alarm_interrupts_an_alert_which_then_resumes(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "discord", "PING", BOX_PRIORITY_ALERT, 10);
    advance(&f, 1.0);
    const box_accepted_t acc = submit(&f, "smoke", "FIRE", BOX_PRIORITY_ALARM, 3);
    CHECK(acc.placement == BOX_PLACED_PREEMPT);
    CHECK_STR(on_screen(&f), "FIRE");
    advance(&f, 3.05);
    CHECK_STR(on_screen(&f), "PING");
    teardown(&f);
}

static void test_an_alarm_interrupts_another_alarm(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "smoke", "FIRE", BOX_PRIORITY_ALARM, 20);
    submit(&f, "water", "LEAK", BOX_PRIORITY_ALARM, 20);
    CHECK_STR(on_screen(&f), "LEAK");
    teardown(&f);
}

static void test_an_interrupted_event_resumes_ahead_of_waiting_alerts(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "discord", "ALERT A", BOX_PRIORITY_ALERT, 10);
    submit(&f, "slack", "ALERT B", BOX_PRIORITY_ALERT, 10); /* waits: next */
    submit(&f, "smoke", "FIRE", BOX_PRIORITY_ALARM, 2);
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "ALERT A");
    teardown(&f);
}

static void test_nearly_finished_event_is_not_resumed(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "github", "ALMOST DONE", BOX_PRIORITY_NORMAL, 3);
    advance(&f, 2.0); /* 1s left: under BOX_MIN_RESUME */
    submit(&f, "discord", "PING", BOX_PRIORITY_ALERT, 2);
    CHECK(box_scheduler_queue_depth(f.s) == 0);
    advance(&f, 2.05);
    CHECK_STR(on_screen(&f), "blank");
    teardown(&f);
}

static void test_ambient_never_interrupts(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "github", "NORMAL", BOX_PRIORITY_NORMAL, 5);
    const box_accepted_t acc = submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    CHECK(acc.placement == BOX_PLACED_AMBIENT);
    CHECK_STR(on_screen(&f), "NORMAL");
    submit(&f, "discord", "ALERT", BOX_PRIORITY_ALERT, 5);
    submit(&f, "weather", "RAINY", BOX_PRIORITY_AMBIENT, 12);
    CHECK_STR(on_screen(&f), "ALERT");
    teardown(&f);
}

static void test_ambient_rotation_advances_and_refreshes_in_place(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 4);
    submit(&f, "clock", "12:00", BOX_PRIORITY_AMBIENT, 4);
    CHECK_STR(on_screen(&f), "SUNNY");
    advance(&f, 4.05);
    CHECK_STR(on_screen(&f), "12:00");
    /* Refreshing the card on screen swaps it in place, keeping the slot. */
    submit(&f, "clock", "12:01", BOX_PRIORITY_AMBIENT, 4);
    CHECK_STR(on_screen(&f), "12:01");
    box_panel_state_t *state = malloc(sizeof(*state));
    box_scheduler_state(f.s, state);
    CHECK(state->ambient_count == 2);
    CHECK_STR(state->priority, "ambient");
    CHECK_STR(state->source, "clock");
    free(state);
    advance(&f, 4.05);
    CHECK_STR(on_screen(&f), "SUNNY");
    teardown(&f);
}

static void test_a_refresh_of_another_card_does_not_jump_unless_focused(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    submit(&f, "clock", "12:00", BOX_PRIORITY_AMBIENT, 12);
    submit(&f, "clock", "12:01", BOX_PRIORITY_AMBIENT, 12);
    CHECK_STR(on_screen(&f), "SUNNY");
    submit_full(&f, "clock", "12:02", BOX_PRIORITY_AMBIENT, 12, true);
    CHECK_STR(on_screen(&f), "12:02");
    teardown(&f);
}

static void test_clear_drops_the_queue_and_keeps_ambient(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    submit(&f, "a", "ONE", BOX_PRIORITY_NORMAL, 5);
    submit(&f, "b", "TWO", BOX_PRIORITY_NORMAL, 5);
    box_scheduler_clear(f.s, f.now);
    CHECK(box_scheduler_queue_depth(f.s) == 0);
    CHECK_STR(on_screen(&f), "SUNNY");
    box_scheduler_clear_ambient(f.s, f.now);
    CHECK_STR(on_screen(&f), "blank");
    teardown(&f);
}

static void test_remove_ambient(void)
{
    fixture_t f;
    setup(&f);
    submit(&f, "box", "NO WI-FI", BOX_PRIORITY_AMBIENT, 12);
    submit(&f, "weather", "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    CHECK_STR(on_screen(&f), "NO WI-FI");
    CHECK(box_scheduler_remove_ambient(f.s, "box", f.now));
    CHECK(!box_scheduler_remove_ambient(f.s, "box", f.now));
    CHECK_STR(on_screen(&f), "SUNNY");
    CHECK(box_scheduler_remove_ambient(f.s, "weather", f.now));
    CHECK_STR(on_screen(&f), "blank");
    teardown(&f);
}

static void test_state_reports_colours(void)
{
    fixture_t f;
    setup(&f);
    box_panel_state_t *state = malloc(sizeof(*state));
    box_scheduler_state(f.s, state);
    CHECK_STR(state->scene, "blank");
    CHECK(state->priority == NULL && state->source[0] == '\0');
    submit(&f, "discord", "HI", BOX_PRIORITY_NORMAL, 5);
    box_scheduler_state(f.s, state);
    CHECK_STR(state->scene, "text");
    CHECK_STR(state->text, "HI");
    CHECK(state->color.r == 0x58 && state->color.b == 0xF2);
    free(state);
    teardown(&f);
}

static void test_a_full_queue_refuses_without_side_effects(void)
{
    fixture_t f;
    setup(&f);
    char source[16];
    submit(&f, "on-screen", "NOW", BOX_PRIORITY_NORMAL, 5);
    for (int i = 0; i < BOX_QUEUE_MAX; ++i) {
        snprintf(source, sizeof(source), "s%d", i);
        submit(&f, source, "WAIT", BOX_PRIORITY_NORMAL, 5);
    }
    CHECK(box_scheduler_queue_depth(f.s) == BOX_QUEUE_MAX);
    box_event_t ev;
    box_event_text(&ev, "newcomer", "NOPE", BOX_PRIORITY_ALARM, 5);
    box_accepted_t acc;
    box_error_t err;
    CHECK(!box_scheduler_submit(f.s, &ev, false, f.now, &acc, &err));
    CHECK(err.status == 503 && ev.scene != NULL);
    CHECK_STR(on_screen(&f), "NOW");
    box_event_clear(&ev);
    /* A source already waiting replaces its own entry, so it still fits. */
    submit(&f, "s0", "REPLACED", BOX_PRIORITY_NORMAL, 5);
    CHECK(box_scheduler_queue_depth(f.s) == BOX_QUEUE_MAX);
    teardown(&f);
}

static void test_a_full_rotation_refuses_new_sources_only(void)
{
    fixture_t f;
    setup(&f);
    char source[16];
    for (int i = 0; i < BOX_AMBIENT_MAX; ++i) {
        snprintf(source, sizeof(source), "card%d", i);
        submit(&f, source, "CARD", BOX_PRIORITY_AMBIENT, 5);
    }
    box_event_t ev;
    box_event_text(&ev, "one-too-many", "X", BOX_PRIORITY_AMBIENT, 5);
    box_accepted_t acc;
    box_error_t err;
    CHECK(!box_scheduler_submit(f.s, &ev, false, f.now, &acc, &err) && err.status == 503);
    box_event_clear(&ev);
    submit(&f, "card3", "UPDATED", BOX_PRIORITY_AMBIENT, 5);
    teardown(&f);
}

static void test_scenes_outlive_the_scheduler_during_a_fade(void)
{
    /* The engine keeps the outgoing scene for the crossfade after the
     * scheduler has freed the event that owned it. */
    fixture_t f;
    setup(&f);
    submit(&f, "a", "GONE SOON", BOX_PRIORITY_NORMAL, 1);
    advance(&f, 1.05);
    box_scheduler_clear(f.s, f.now);
    (void)box_engine_render(&f.engine, f.now + 0.1); /* ASan flags a dangling scene */
    teardown(&f);
}

void run_scheduler_tests(void)
{
    RUN(test_ambient_shows_when_nothing_queued);
    RUN(test_normal_takes_the_panel_then_returns_to_ambient);
    RUN(test_events_queue_in_order);
    RUN(test_burst_from_one_source_is_coalesced);
    RUN(test_alert_preempts_and_the_preempted_event_resumes);
    RUN(test_alert_waits_for_an_alert_already_on_the_panel);
    RUN(test_a_waiting_alert_shows_before_queued_normals);
    RUN(test_alarm_interrupts_an_alert_which_then_resumes);
    RUN(test_an_alarm_interrupts_another_alarm);
    RUN(test_an_interrupted_event_resumes_ahead_of_waiting_alerts);
    RUN(test_nearly_finished_event_is_not_resumed);
    RUN(test_ambient_never_interrupts);
    RUN(test_ambient_rotation_advances_and_refreshes_in_place);
    RUN(test_a_refresh_of_another_card_does_not_jump_unless_focused);
    RUN(test_clear_drops_the_queue_and_keeps_ambient);
    RUN(test_remove_ambient);
    RUN(test_state_reports_colours);
    RUN(test_a_full_queue_refuses_without_side_effects);
    RUN(test_a_full_rotation_refuses_new_sources_only);
    RUN(test_scenes_outlive_the_scheduler_during_a_fade);
}
