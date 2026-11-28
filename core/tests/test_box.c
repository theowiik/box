// The box object through a fake display, the idle clock, and the API's JSON.
#include <stdlib.h>

#include "box/api.h"
#include "box/box.h"
#include "check.h"

typedef struct {
    int shows;
    int lit; /* lit pixels in the last frame shown */
    float brightness;
    int brightness_calls;
} fake_display_t;

static bool fake_show(void *ctx, const box_frame_t *frame)
{
    fake_display_t *d = ctx;
    d->shows++;
    d->lit = 0;
    for (size_t i = 0; i < box_frame_bytes(frame); i += 3) {
        d->lit += frame->pixels[i] || frame->pixels[i + 1] || frame->pixels[i + 2];
    }
    return true;
}

static bool fake_brightness(void *ctx, float brightness)
{
    fake_display_t *d = ctx;
    d->brightness = brightness;
    d->brightness_calls++;
    return true;
}

typedef struct {
    bool known;
    int hour, minute;
} fake_clock_t;

static bool fake_time(void *ctx, int *hour, int *minute, int *second)
{
    const fake_clock_t *c = ctx;
    *hour = c->hour;
    *minute = c->minute;
    *second = 0;
    return c->known;
}

static void step(box_t *box, double now)
{
    box_show(box, box_render(box, now));
}

static void test_frames_reach_the_display_through_the_interface(void)
{
    fake_display_t d = {0};
    const box_display_t display = {120, 120, fake_show, fake_brightness, &d};
    box_t *box = box_new(&display, 16, 1);
    CHECK(box != NULL);
    step(box, 1.0);
    CHECK(d.shows == 1 && d.lit == 0); /* idle defaults to clear */
    step(box, 1.5);
    CHECK(d.shows == 1); /* nothing changed: the display is not called */
    box_event_t ev;
    box_event_text(&ev, "me", NULL, "HI", BOX_PRIORITY_NORMAL, 5);
    box_accepted_t acc;
    box_error_t err;
    CHECK(box_scheduler_submit(box_scheduler(box), &ev, false, 1.0, &acc, &err));
    step(box, 2.0); /* past the crossfade */
    CHECK(d.shows == 2 && d.lit > 0);
    box_free(box);
}

static void test_brightness_reaches_the_display_only_from_the_render_loop(void)
{
    fake_display_t d = {0};
    const box_display_t display = {16, 16, fake_show, fake_brightness, &d};
    box_t *box = box_new(&display, 16, 1);
    box_request_brightness(box, 0.4f);
    CHECK(d.brightness_calls == 0 && box_brightness(box) == 0.4f);
    const box_frame_t *frame = box_render(box, 1.0);
    CHECK(d.brightness_calls == 0); /* still not: render runs under the lock */
    box_show(box, frame);
    CHECK(d.brightness_calls == 1 && d.brightness == 0.4f);
    step(box, 2.0);
    CHECK(d.brightness_calls == 1); /* applied once */
    box_free(box);
}

static void test_a_display_without_dimming_is_fine(void)
{
    fake_display_t d = {0};
    const box_display_t display = {16, 16, fake_show, NULL, &d};
    box_t *box = box_new(&display, 16, 1);
    box_request_brightness(box, 0.5f);
    step(box, 1.0);
    CHECK(d.shows == 1);
    box_free(box);
}

static void test_the_clock_shows_when_nothing_else_does(void)
{
    fake_display_t d = {0};
    fake_clock_t time = {true, 18, 42};
    const box_display_t display = {96, 96, fake_show, NULL, &d};
    box_t *box = box_new(&display, 16, 1);
    box_scene_t *clock = box_scene_new_clock(fake_time, &time, (box_rgb_t){255, 106, 61});
    box_set_idle(box, clock, 0.0);
    box_scene_release(clock); /* the box keeps its own reference */
    step(box, 1.0);
    CHECK(d.lit > 0);
    box_panel_state_t *state = malloc(sizeof(*state));
    box_scheduler_state(box_scheduler(box), state);
    CHECK_STR(state->content, "clock");
    CHECK_STR(state->text, "18:42");
    CHECK(state->priority == NULL && state->color.r == 255);

    /* An event takes over, then the clock comes back. */
    box_event_t ev;
    box_event_text(&ev, "me", NULL, "HELLO", BOX_PRIORITY_NORMAL, 3);
    box_accepted_t acc;
    box_error_t err;
    box_scheduler_submit(box_scheduler(box), &ev, false, 1.0, &acc, &err);
    step(box, 2.0);
    box_scheduler_state(box_scheduler(box), state);
    CHECK_STR(state->content, "text");
    step(box, 4.5);
    box_scheduler_state(box_scheduler(box), state);
    CHECK_STR(state->content, "clock");

    /* Ambient cards count as something to show. */
    box_event_text(&ev, "weather", NULL, "SUNNY", BOX_PRIORITY_AMBIENT, 12);
    box_scheduler_submit(box_scheduler(box), &ev, false, 5.0, &acc, &err);
    box_scheduler_state(box_scheduler(box), state);
    CHECK_STR(state->content, "text");
    free(state);
    box_free(box);
}

static void test_the_clock_follows_the_time_and_waits_until_it_is_known(void)
{
    fake_display_t d = {0};
    fake_clock_t time = {false, 0, 0};
    const box_display_t display = {96, 96, fake_show, NULL, &d};
    box_t *box = box_new(&display, 16, 1);
    box_scene_t *clock = box_scene_new_clock(fake_time, &time, (box_rgb_t){255, 255, 255});
    box_set_idle(box, clock, 0.0);
    box_scene_release(clock);
    step(box, 1.0);
    CHECK(d.lit == 0); /* no time yet: nothing rather than 00:00 */
    time = (fake_clock_t){true, 9, 5};
    step(box, 2.0);
    const int at_0905 = d.lit;
    CHECK(at_0905 > 0);
    time.minute = 7;
    step(box, 3.0);
    CHECK(d.lit != at_0905); /* the digits changed */
    box_panel_state_t *state = malloc(sizeof(*state));
    box_scheduler_state(box_scheduler(box), state);
    CHECK_STR(state->text, "09:07");
    free(state);
    box_free(box);
}

static void test_api_json(void)
{
    box_panel_state_t *state = calloc(1, sizeof(*state));
    state->content = "text";
    state->style = "default";
    snprintf(state->source, sizeof(state->source), "discord");
    state->priority = "alert";
    snprintf(state->title, sizeof(state->title), "Discord");
    snprintf(state->text, sizeof(state->text), "hi \"there\"");
    state->color = (box_rgb_t){0x58, 0x65, 0xF2};
    state->ambient_count = 1;
    snprintf(state->ambient[0], sizeof(state->ambient[0]), "weather");
    const box_api_info_t info = {480, 480, "square", 96, 25, "smooth", 0.8f, 12.0f, 12.0f};
    char *json = box_api_status(state, &info);
    CHECK(json && strstr(json, "\"showing\":{\"type\":\"text\",\"source\":\"discord\""));
    CHECK(strstr(json, "\"title\":\"Discord\"") && strstr(json, "\"text\":\"hi \\\"there\\\"\""));
    CHECK(strstr(json, "\"ambient\":[\"weather\"]") && strstr(json, "\"brightness\":0.8,"));
    CHECK(strstr(json, "\"fps\":{\"drawn\":12,\"shown\":12}") != NULL);
    box_api_free(json);
    state->source[0] = state->title[0] = '\0';
    state->priority = NULL;
    json = box_api_status(state, &info);
    CHECK(strstr(json, "\"source\":null") && strstr(json, "\"title\":null"));
    box_api_free(json);
    free(state);

    json = box_api_display(&info);
    CHECK(json && strstr(json, "\"width\":480") && strstr(json, "\"shape\":\"square\""));
    CHECK(strstr(json, "\"content\":[\"text\",\"image\",\"color\",\"clear\",\"glance\"]") &&
          strstr(json, "\"led_grid\":96") && strstr(json, "\"image_bytes\":4194304"));
    box_api_free(json);

    const box_accepted_t acc = {"abc123def456", "me", BOX_PRIORITY_NORMAL, BOX_PLACED_QUEUED,
                                8.0, 2};
    json = box_api_accepted(&acc);
    CHECK_STR(json, "{\"id\":\"abc123def456\",\"source\":\"me\",\"priority\":\"normal\","
                    "\"duration\":8,\"queued\":2}");
    box_api_free(json);

    bool has_b, has_s;
    float b;
    box_style_t st;
    box_error_t err = {0};
    CHECK(box_api_parse_settings("{\"brightness\":0.25}", 19, &has_b, &b, &has_s, &st, &err) &&
          has_b && !has_s && b == 0.25f);
    CHECK(box_api_parse_settings("{\"style\":\"led\"}", 15, &has_b, &b, &has_s, &st, &err) &&
          has_s && st == BOX_STYLE_LED && !has_b);
    CHECK(!box_api_parse_settings("{\"brightness\":1.5}", 18, &has_b, &b, &has_s, &st, &err));
    CHECK(!box_api_parse_settings("{\"style\":\"neon\"}", 16, &has_b, &b, &has_s, &st, &err));
    CHECK(!box_api_parse_settings("{}", 2, &has_b, &b, &has_s, &st, &err) && err.status == 422);
}

void run_box_tests(void)
{
    RUN(test_frames_reach_the_display_through_the_interface);
    RUN(test_brightness_reaches_the_display_only_from_the_render_loop);
    RUN(test_a_display_without_dimming_is_fine);
    RUN(test_the_clock_shows_when_nothing_else_does);
    RUN(test_the_clock_follows_the_time_and_waits_until_it_is_known);
    RUN(test_api_json);
}
