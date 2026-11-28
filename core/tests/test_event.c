// The event contract: content types, defaults, styling and validation.
#include <stdlib.h>

#include "base64.h"
#include "box/event.h"
#include "check.h"

static bool parse(const char *json, box_event_t *ev, box_error_t *err)
{
    return box_event_parse(json, strlen(json), ev, err);
}

static bool refused(const char *json, int status)
{
    box_event_t ev;
    box_error_t err;
    const bool ok = parse(json, &ev, &err);
    if (ok) {
        box_event_clear(&ev);
        fprintf(stderr, "  accepted: %s\n", json);
        return false;
    }
    if (err.status != status) {
        fprintf(stderr, "  %s -> %d %s\n", json, err.status, err.message);
    }
    return err.status == status && err.message[0] && ev.scene == NULL;
}

static void test_text_defaults(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"content\":{\"type\":\"text\",\"text\":\"hi\"}}", &ev, &err));
    CHECK_STR(ev.source, "api");
    CHECK(ev.priority == BOX_PRIORITY_NORMAL);
    CHECK_NEAR(ev.duration, 8.0, 1e-9);
    CHECK_STR(box_scene_content_name(ev.scene), "text");
    CHECK_STR(box_scene_text(ev.scene), "hi");
    CHECK_STR(box_scene_title(ev.scene), "");
    CHECK(box_scene_style(ev.scene) == BOX_STYLE_DEFAULT);
    CHECK(ev.color.g == 0xE6); /* unknown source: the priority's colour */
    box_event_clear(&ev);
}

static void test_title_colour_align_and_style(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"source\":\"discord\",\"priority\":\"alert\",\"style\":\"led\","
                "\"content\":{\"type\":\"text\",\"title\":\"Discord\",\"text\":\"@theo\","
                "\"align\":\"left\"}}",
                &ev, &err));
    CHECK(ev.color.r == 0x58 && ev.color.b == 0xF2); /* blurple */
    CHECK_STR(box_scene_title(ev.scene), "Discord");
    CHECK(box_scene_style(ev.scene) == BOX_STYLE_LED);
    CHECK_NEAR(ev.duration, 10.0, 1e-9);
    box_event_clear(&ev);
    CHECK(parse("{\"content\":{\"type\":\"text\",\"text\":\"x\",\"color\":\"#f00\"}}", &ev, &err));
    CHECK(ev.color.r == 255 && ev.color.g == 0);
    box_event_clear(&ev);
}

static void test_color_clear_and_image(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"content\":{\"type\":\"color\",\"color\":\"#102030\"}}", &ev, &err));
    CHECK(box_scene_content(ev.scene) == BOX_CONTENT_COLOR && ev.color.b == 0x30);
    box_event_clear(&ev);
    CHECK(parse("{\"content\":{\"type\":\"clear\"}}", &ev, &err));
    CHECK(box_scene_content(ev.scene) == BOX_CONTENT_CLEAR);
    box_event_clear(&ev);
    /* One 2x1 frame: 6 bytes -> "AAAAAAAA". */
    CHECK(parse("{\"content\":{\"type\":\"image\",\"width\":2,\"height\":1,\"fps\":5,"
                "\"loop\":false,\"scaling\":\"smooth\",\"data\":\"AAAAAAAA\"}}",
                &ev, &err));
    CHECK(box_scene_content(ev.scene) == BOX_CONTENT_IMAGE);
    box_event_clear(&ev);
}

static void test_invalid_events_are_refused(void)
{
    CHECK(refused("not json", 400));
    CHECK(refused("[1,2]", 422));
    CHECK(refused("{}", 422));
    CHECK(refused("{\"content\":{\"type\":\"video\"}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"text\"}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"text\",\"text\":\"x\",\"color\":\"red\"}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"text\",\"text\":\"x\",\"align\":\"right\"}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"text\",\"text\":\"x\",\"title\":7}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"color\"}}", 422));
    CHECK(refused("{\"style\":\"neon\",\"content\":{\"type\":\"clear\"}}", 422));
    CHECK(refused("{\"priority\":\"urgent\",\"content\":{\"type\":\"clear\"}}", 422));
    CHECK(refused("{\"duration\":0,\"content\":{\"type\":\"clear\"}}", 422));
    CHECK(refused("{\"source\":\"has space\",\"content\":{\"type\":\"clear\"}}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"text\",\"text\":\"the old API\"}}", 422));
}

static void test_text_limits(void)
{
    char json[1300];
    char text[600];
    memset(text, 'A', 513);
    text[513] = '\0';
    snprintf(json, sizeof(json), "{\"content\":{\"type\":\"text\",\"text\":\"%s\"}}", text);
    CHECK(refused(json, 422));
    text[81] = '\0';
    snprintf(json, sizeof(json),
             "{\"content\":{\"type\":\"text\",\"text\":\"x\",\"title\":\"%s\"}}", text);
    CHECK(refused(json, 422));
}

static void test_image_validation(void)
{
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":2,\"height\":2,\"data\":\"AAAAAAAA\"}}",
                  422)); /* 6 bytes is not a 2x2 frame */
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":2,\"height\":1,\"data\":\"AA!AAAAA\"}}",
                  422));
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":2,\"height\":1,\"data\":\"\"}}", 422));
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":1.5,\"height\":1,\"data\":\"AAAA\"}}",
                  422));
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":1,\"height\":1,"
                  "\"scaling\":\"fuzzy\",\"data\":\"AAAA\"}}",
                  422));
    const size_t chars = (BOX_IMAGE_MAX_BYTES / 3 + 4) * 4;
    char *big = malloc(chars + 128);
    const int head =
        sprintf(big, "{\"content\":{\"type\":\"image\",\"width\":1,\"height\":1,\"data\":\"");
    memset(big + head, 'A', chars);
    strcpy(big + head + chars, "\"}}");
    CHECK(refused(big, 422));
    free(big);
    box_images_set_budget(box_images_in_use() + 3);
    CHECK(refused("{\"content\":{\"type\":\"image\",\"width\":2,\"height\":1,\"data\":\"AAAAAAAA\"}}",
                  503));
    box_images_set_budget(12u * 1024u * 1024u);
}

static void test_base64(void)
{
    uint8_t out[8];
    CHECK(box_base64_decoded_size("aGk=", 4) == 2);
    CHECK(box_base64_decode("aGk=", 4, out) && out[0] == 'h' && out[1] == 'i');
    CHECK(box_base64_decode("aGVsbG8=", 8, out) && memcmp(out, "hello", 5) == 0);
    CHECK(!box_base64_decode("aG=k", 4, out));
    CHECK(!box_base64_decode("aGk", 3, out));
}

static void test_code_built_text_event(void)
{
    box_event_t ev;
    CHECK(box_event_text(&ev, "box", "box.local", "192.168.0.228", BOX_PRIORITY_NORMAL, 0));
    CHECK_NEAR(ev.duration, 8.0, 1e-9);
    CHECK_STR(box_scene_title(ev.scene), "box.local");
    box_event_clear(&ev);
}

static int lit_pixels(const box_frame_t *f)
{
    int n = 0;
    for (size_t i = 0; i < box_frame_bytes(f); i += 3) {
        n += f->pixels[i] || f->pixels[i + 1] || f->pixels[i + 2];
    }
    return n;
}

static int s_minute = 46;
static bool test_clock(void *ctx, int *hour, int *minute)
{
    *hour = 15;
    *minute = s_minute;
    return true;
}

static void test_glance_dots_move_then_rest(void)
{
    box_scene_set_clock(test_clock, NULL);
    box_scene_t *s = box_scene_new_glance("Friday", "", "1\xc2\xb0", "Clear", 0, 0, (box_rgb_t){0, 0, 0});
    /* Find a still moment: between the smiley's blinks and looks. */
    double still = -1;
    for (double t = 0.1; t < 20 && still < 0; t += 0.05) {
        if (box_scene_version(s, t) == box_scene_version(s, t + 0.2)) {
            still = t;
        }
    }
    CHECK(still >= 0); /* a resting glance costs nothing */
    s_minute = 47;
    const uint64_t a = box_scene_version(s, still), b = box_scene_version(s, still + 0.1);
    CHECK(a != b); /* the dots fly */
    box_frame_t f;
    CHECK(box_frame_init(&f, 240, 240));
    box_scene_render(s, &f, still + 0.3, BOX_STYLE_SMOOTH);
    CHECK(lit_pixels(&f) > 500);
    box_frame_free(&f);
    /* Shown afresh, its clock restarts at 0: the dots still move on. */
    box_scene_version(s, still + 30);
    s_minute = 48;
    CHECK(box_scene_version(s, 0.2) != box_scene_version(s, 0.3));
    box_scene_set_clock(NULL, NULL);
    box_scene_release(s);
}

static void test_glance(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"content\":{\"type\":\"glance\",\"date\":\"Friday 27 November\",\"time\":\"15:46\","
                "\"temp\":\"1\u00b0\",\"condition\":\"Partly cloudy\",\"sky\":\"partly\",\"mood\":\"good\"}}",
                &ev, &err));
    CHECK_STR(box_scene_content_name(ev.scene), "glance");
    CHECK_STR(box_scene_title(ev.scene), "Friday 27 November");
    CHECK_STR(box_scene_text(ev.scene), "15:46 1\xc2\xb0 Partly cloudy");
    box_frame_t f;
    CHECK(box_frame_init(&f, 240, 240));
    for (int sky = 0; sky < 8; ++sky) {
        for (int mood = 0; mood < 4; ++mood) {
            box_scene_t *s = box_scene_new_glance("Friday", "15:46", "-12\xc2\xb0", "Rainy", sky, mood,
                                                  (box_rgb_t){80, 160, 255});
            box_scene_render(s, &f, 0, BOX_STYLE_SMOOTH);
            CHECK(lit_pixels(&f) > 2000 && lit_pixels(&f) < 240 * 120); /* black, mostly */
            box_frame_t grid;
            CHECK(box_frame_init(&grid, 48, 48));
            box_scene_render(s, &grid, 0, BOX_STYLE_LED);
            CHECK(lit_pixels(&grid) > 100);
            box_frame_free(&grid);
            box_scene_release(s);
        }
    }
    box_frame_free(&f);
    box_event_clear(&ev);
    CHECK(refused("{\"content\":{\"type\":\"glance\",\"time\":\"1\",\"sky\":\"lava\"}}", 422));
}

void run_event_tests(void)
{
    RUN(test_glance);
    RUN(test_glance_dots_move_then_rest);
    RUN(test_text_defaults);
    RUN(test_title_colour_align_and_style);
    RUN(test_color_clear_and_image);
    RUN(test_invalid_events_are_refused);
    RUN(test_text_limits);
    RUN(test_image_validation);
    RUN(test_base64);
    RUN(test_code_built_text_event);
}
