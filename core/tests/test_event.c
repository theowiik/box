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

void run_event_tests(void)
{
    RUN(test_text_defaults);
    RUN(test_title_colour_align_and_style);
    RUN(test_color_clear_and_image);
    RUN(test_invalid_events_are_refused);
    RUN(test_text_limits);
    RUN(test_image_validation);
    RUN(test_base64);
    RUN(test_code_built_text_event);
}
