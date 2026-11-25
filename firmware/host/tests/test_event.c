// The event contract: defaults, styling and validation. Ported from the
// Python box's tests/test_events_api.py (minus the HTTP layer).
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
        return false;
    }
    if (err.status != status) {
        fprintf(stderr, "  %s -> %d %s\n", json, err.status, err.message);
    }
    return err.status == status && err.message[0] && ev.scene == NULL;
}

static void test_text_event_defaults(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"scene\":{\"kind\":\"text\",\"text\":\"hi\"}}", &ev, &err));
    CHECK_STR(ev.source, "api");
    CHECK(ev.priority == BOX_PRIORITY_NORMAL);
    CHECK_NEAR(ev.duration, 8.0, 1e-9);
    CHECK_STR(box_scene_text(ev.scene), "hi");
    /* Unknown source: the priority's colour. */
    CHECK(ev.color.r == 0x00 && ev.color.g == 0xE6 && ev.color.b == 0x76);
    box_event_clear(&ev);
}

static void test_source_styling_and_explicit_colour(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"source\":\"discord\",\"priority\":\"alert\","
                "\"scene\":{\"kind\":\"text\",\"text\":\"@theo\"}}",
                &ev, &err));
    CHECK(ev.color.r == 0x58 && ev.color.g == 0x65 && ev.color.b == 0xF2); /* blurple */
    CHECK_NEAR(ev.duration, 10.0, 1e-9);
    box_event_clear(&ev);
    CHECK(parse("{\"source\":\"discord\",\"duration\":3,"
                "\"scene\":{\"kind\":\"text\",\"text\":\"x\",\"color\":\"#f00\"}}",
                &ev, &err));
    CHECK(ev.color.r == 255 && ev.color.g == 0);
    CHECK_NEAR(ev.duration, 3.0, 1e-9);
    box_event_clear(&ev);
}

static void test_other_scene_kinds(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(parse("{\"scene\":{\"kind\":\"solid\",\"color\":\"#102030\"}}", &ev, &err));
    CHECK(box_scene_kind(ev.scene) == BOX_SCENE_SOLID && ev.color.b == 0x30);
    box_event_clear(&ev);
    CHECK(parse("{\"scene\":{\"kind\":\"blank\"}}", &ev, &err));
    CHECK(box_scene_kind(ev.scene) == BOX_SCENE_BLANK);
    box_event_clear(&ev);
}

static void test_malformed_events_are_refused(void)
{
    CHECK(refused("not json", 400));
    CHECK(refused("[1,2]", 422));
    CHECK(refused("{}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"nope\"}}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"text\"}}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"text\",\"text\":\"x\",\"color\":\"red\"}}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"solid\"}}", 422));
    CHECK(refused("{\"priority\":\"urgent\",\"scene\":{\"kind\":\"blank\"}}", 422));
    CHECK(refused("{\"duration\":0,\"scene\":{\"kind\":\"blank\"}}", 422));
    CHECK(refused("{\"duration\":3601,\"scene\":{\"kind\":\"blank\"}}", 422));
    CHECK(refused("{\"source\":\"has space\",\"scene\":{\"kind\":\"blank\"}}", 422));
    CHECK(refused("{\"source\":\"\",\"scene\":{\"kind\":\"blank\"}}", 422));
}

static void test_visuals_point_to_the_sources_service(void)
{
    box_event_t ev;
    box_error_t err;
    CHECK(!parse("{\"scene\":{\"kind\":\"visual\",\"name\":\"beer\"}}", &ev, &err));
    CHECK(err.status == 422 && strstr(err.message, "box-sources visual") != NULL);
}

static void test_text_length_limit(void)
{
    char json[1200];
    char text[600];
    memset(text, 'A', 512);
    text[512] = '\0';
    snprintf(json, sizeof(json), "{\"scene\":{\"kind\":\"text\",\"text\":\"%s\"}}", text);
    box_event_t ev;
    box_error_t err;
    CHECK(parse(json, &ev, &err));
    box_event_clear(&ev);
    text[512] = 'A';
    text[513] = '\0';
    snprintf(json, sizeof(json), "{\"scene\":{\"kind\":\"text\",\"text\":\"%s\"}}", text);
    CHECK(refused(json, 422));
}

static void test_frames(void)
{
    box_event_t ev;
    box_error_t err;
    /* One 2x1 frame: 6 bytes -> "AAAAAAAA". */
    CHECK(parse("{\"source\":\"gif\",\"scene\":{\"kind\":\"frames\",\"width\":2,\"height\":1,"
                "\"fps\":5,\"loop\":false,\"data\":\"AAAAAAAA\"}}",
                &ev, &err));
    CHECK(box_scene_kind(ev.scene) == BOX_SCENE_FRAMES);
    box_event_clear(&ev);
    /* 6 bytes is not a whole number of 2x2 frames (12 bytes each). */
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":2,\"height\":2,\"data\":\"AAAAAAAA\"}}",
                  422));
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":2,\"height\":1,\"data\":\"AA!AAAAA\"}}",
                  422));
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":2,\"height\":1,\"data\":\"\"}}", 422));
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":0,\"height\":1,\"data\":\"AAAA\"}}",
                  422));
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":1.5,\"height\":1,\"data\":\"AAAA\"}}",
                  422));
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":1,\"height\":1,\"fps\":0,"
                  "\"data\":\"AAAA\"}}",
                  422));
}

static void test_oversized_animation_refused_before_decoding(void)
{
    const size_t chars = (BOX_FRAMES_MAX_BYTES / 3 + 4) * 4;
    char *json = malloc(chars + 128);
    const int head = sprintf(json, "{\"scene\":{\"kind\":\"frames\",\"width\":1,\"height\":1,\"data\":\"");
    memset(json + head, 'A', chars);
    strcpy(json + head + chars, "\"}}");
    CHECK(refused(json, 422));
    free(json);
}

static void test_animation_budget_is_a_503(void)
{
    box_frames_set_budget(box_frames_in_use() + 3);
    CHECK(refused("{\"scene\":{\"kind\":\"frames\",\"width\":2,\"height\":1,\"data\":\"AAAAAAAA\"}}",
                  503));
    box_frames_set_budget(12u * 1024u * 1024u);
}

static void test_base64(void)
{
    uint8_t out[8];
    CHECK(box_base64_decoded_size("aGk=", 4) == 2);
    CHECK(box_base64_decode("aGk=", 4, out) && out[0] == 'h' && out[1] == 'i');
    CHECK(box_base64_decoded_size("aGVsbG8=", 8) == 5);
    CHECK(box_base64_decode("aGVsbG8=", 8, out) && memcmp(out, "hello", 5) == 0);
    CHECK(!box_base64_decode("aG=k", 4, out)); /* padding in the middle */
    CHECK(!box_base64_decode("aGk", 3, out));  /* wrong length */
}

static void test_code_built_text_event(void)
{
    box_event_t ev;
    CHECK(box_event_text(&ev, "box", "BOX.LOCAL", BOX_PRIORITY_NORMAL, 0));
    CHECK_NEAR(ev.duration, 8.0, 1e-9);
    CHECK_STR(box_scene_text(ev.scene), "BOX.LOCAL");
    box_event_clear(&ev);
}

void run_event_tests(void)
{
    RUN(test_text_event_defaults);
    RUN(test_source_styling_and_explicit_colour);
    RUN(test_other_scene_kinds);
    RUN(test_malformed_events_are_refused);
    RUN(test_visuals_point_to_the_sources_service);
    RUN(test_text_length_limit);
    RUN(test_frames);
    RUN(test_oversized_animation_refused_before_decoding);
    RUN(test_animation_budget_is_a_503);
    RUN(test_base64);
    RUN(test_code_built_text_event);
}
