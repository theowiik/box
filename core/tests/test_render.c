// Frames, the bitmap font (led style), image scenes and the engine.
#include <stdlib.h>

#include "box/engine.h"
#include "box/scene.h"
#include "box/text.h"
#include "check.h"

static const box_rgb_t WHITE = {255, 255, 255};

static box_rgb_t px(const box_frame_t *f, int x, int y)
{
    const uint8_t *p = f->pixels + (y * f->width + x) * 3;
    return (box_rgb_t){p[0], p[1], p[2]};
}

static bool lit(const box_frame_t *f, int x, int y)
{
    const box_rgb_t c = px(f, x, y);
    return c.r || c.g || c.b;
}

static int lit_count(const box_frame_t *f)
{
    int n = 0;
    for (int y = 0; y < f->height; ++y) {
        for (int x = 0; x < f->width; ++x) {
            n += lit(f, x, y);
        }
    }
    return n;
}

static box_scene_t *text_scene(const char *title, const char *text)
{
    return box_scene_new_text(title, strlen(title), text, strlen(text), WHITE, BOX_ALIGN_CENTER);
}

static void test_hex_roundtrip(void)
{
    box_rgb_t c;
    char out[8];
    CHECK(box_rgb_parse("#1A2b3C", &c));
    box_rgb_format(c, out);
    CHECK_STR(out, "#1A2B3C");
    CHECK(box_rgb_parse("f80", &c) && c.r == 0xFF && c.g == 0x88 && c.b == 0x00);
    CHECK(!box_rgb_parse("#12345", &c));
    CHECK(!box_rgb_parse("#GG0000", &c));
}

static void test_frame_clear_set_and_bounds(void)
{
    box_frame_t f;
    CHECK(box_frame_init(&f, 4, 3));
    box_frame_clear(&f, (box_rgb_t){1, 2, 3});
    CHECK(px(&f, 3, 2).r == 1 && px(&f, 3, 2).b == 3);
    box_frame_set_px(&f, 1, 1, WHITE);
    CHECK(px(&f, 1, 1).g == 255);
    box_frame_set_px(&f, -1, 0, WHITE); /* ignored, not a crash */
    box_frame_set_px(&f, 4, 0, WHITE);
    box_frame_set_px(&f, 0, 3, WHITE);
    box_frame_free(&f);
}

static box_text_layout_t bitmap_layout(const char *text, int size)
{
    uint32_t cps[600];
    const size_t n = box_utf8_decode(text, strlen(text), cps, 600);
    box_text_layout_t layout;
    box_text_layout(cps, n, size, size, &layout);
    return layout;
}

static void test_led_text_wraps_whole_words_and_fits(void)
{
    const box_text_layout_t l = bitmap_layout("22°C SUNNY", 96);
    CHECK(l.scale == 3 && l.line_count == 2 && !l.truncated);
    const box_text_layout_t nl = bitmap_layout("box.local\n192.168.0.228", 96);
    CHECK(nl.line_count == 2 && nl.lines[0].count == 9 && nl.lines[1].count == 13);
    const box_text_layout_t word = bitmap_layout("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 96);
    CHECK(word.scale == 2 && word.line_count == 4);
    char lots[520];
    for (int i = 0; i < 100; ++i) {
        memcpy(lots + i * 5, "WORD ", 5);
    }
    lots[500] = '\0';
    const box_text_layout_t cut = bitmap_layout(lots, 96);
    CHECK(cut.truncated && cut.scale == 1 && cut.line_count == 11);
}

static void test_led_text_puts_the_title_above_dimmer(void)
{
    box_frame_t f;
    box_frame_init(&f, 96, 96);
    box_scene_t *s = text_scene("DISCORD", "HI");
    box_scene_render(s, &f, 0, BOX_STYLE_LED);
    int top_max = 0, bottom_max = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 96; ++x) {
            top_max = px(&f, x, y).r > top_max ? px(&f, x, y).r : top_max;
            bottom_max = px(&f, x, y + 48).r > bottom_max ? px(&f, x, y + 48).r : bottom_max;
        }
    }
    CHECK(top_max > 0 && top_max < 200 && bottom_max == 255);
    box_scene_release(s);
    box_frame_free(&f);
}

static void test_font_and_utf8(void)
{
    const uint8_t *fallback = box_font_glyph('?');
    CHECK(box_font_glyph(0x00E5) == box_font_glyph(0x00C5)); /* å -> Å */
    CHECK(box_font_glyph('a') == box_font_glyph('A'));
    CHECK(box_font_glyph(0x2603) == fallback); /* snowman */
    CHECK(box_text_width(3, 1) == 17 && box_text_width(2, 7) == 77);
    uint32_t cps[8];
    const char *s = "Å\xFFo";
    CHECK(box_utf8_decode(s, strlen(s), cps, 8) == 3);
    CHECK(cps[0] == 0xC5 && cps[1] == 0xFFFD && cps[2] == 'o');
}

/* Three 2x2 frames, each a flat grey of 10, 20, 30. */
static box_scene_t *three_frames(float fps, bool loop, box_scaling_t scaling)
{
    const size_t size = 3 * 2 * 2 * 3;
    uint8_t *data = malloc(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = (uint8_t)(10 * (1 + i / 12));
    }
    return box_scene_new_image(data, size, 2, 2, fps, loop, scaling);
}

static void test_images_play_loop_and_hold(void)
{
    box_frame_t f;
    box_frame_init(&f, 2, 2);
    box_scene_t *looping = three_frames(10.0f, true, BOX_SCALING_PIXELATED);
    box_scene_render(looping, &f, 0.0, BOX_STYLE_SMOOTH);
    CHECK(px(&f, 0, 0).r == 10);
    box_scene_render(looping, &f, 0.15, BOX_STYLE_SMOOTH);
    CHECK(px(&f, 0, 0).r == 20);
    CHECK(box_scene_version(looping, 0.35) == 0); /* frame 3 wraps to 0 */
    box_scene_t *once = three_frames(10.0f, false, BOX_SCALING_PIXELATED);
    CHECK(box_scene_version(once, 5.0) == 2);
    box_scene_release(looping);
    box_scene_release(once);
    box_frame_free(&f);
}

static void test_images_fit_the_screen(void)
{
    /* A 2x1 image (red, blue) on a 10x10 screen: whole-multiple scaling to
     * 10x5, centred vertically; black above and below. */
    uint8_t *data = malloc(6);
    const uint8_t pixels[6] = {255, 0, 0, 0, 0, 255};
    memcpy(data, pixels, 6);
    box_scene_t *s = box_scene_new_image(data, 6, 2, 1, 1.0f, true, BOX_SCALING_PIXELATED);
    box_frame_t f;
    box_frame_init(&f, 10, 10);
    box_scene_render(s, &f, 0, BOX_STYLE_SMOOTH);
    CHECK(!lit(&f, 0, 1) && px(&f, 0, 3).r == 255 && px(&f, 9, 6).b == 255 && !lit(&f, 9, 8));
    CHECK(px(&f, 4, 4).r == 255 && px(&f, 5, 4).b == 255); /* a hard edge */
    box_scene_release(s);

    /* Smooth scaling blends across that edge. */
    data = malloc(6);
    memcpy(data, pixels, 6);
    s = box_scene_new_image(data, 6, 2, 1, 1.0f, true, BOX_SCALING_SMOOTH);
    box_scene_render(s, &f, 0, BOX_STYLE_SMOOTH);
    const box_rgb_t mid = px(&f, 4, 4);
    CHECK(mid.r > 0 && mid.r < 255 && mid.b > 0 && mid.b < 255);
    box_scene_release(s);
    box_frame_free(&f);
}

static void test_ragged_images_and_the_budget(void)
{
    const size_t before = box_images_in_use();
    CHECK(box_scene_new_image(malloc(13), 13, 2, 2, 1.0f, true, BOX_SCALING_PIXELATED) == NULL);
    CHECK(box_images_in_use() == before);
    box_scene_t *s = three_frames(1.0f, true, BOX_SCALING_PIXELATED);
    CHECK(box_images_in_use() == before + 36);
    box_images_set_budget(before + 40);
    CHECK(box_images_budget_allows(4) && !box_images_budget_allows(5));
    box_scene_release(s);
    CHECK(box_images_in_use() == before);
    box_images_set_budget(12u * 1024u * 1024u);
}

static void test_engine_draws_only_when_something_changes(void)
{
    box_engine_t e;
    CHECK(box_engine_init(&e, 48, 48, 16));
    CHECK(box_engine_render(&e, 0.0) != NULL); /* the first frame */
    CHECK(box_engine_render(&e, 0.1) == NULL); /* still black */
    box_scene_t *s = text_scene("", "HI");
    box_engine_set_scene(&e, s, 1.0);
    box_scene_release(s);
    const box_frame_t *mid = box_engine_render(&e, 1.0 + BOX_TRANSITION / 2);
    CHECK(mid != NULL && lit_count(mid) > 0);
    const box_frame_t *done = box_engine_render(&e, 1.0 + BOX_TRANSITION + 0.01);
    CHECK(done != NULL);
    const int smooth_lit = lit_count(done);
    CHECK(box_engine_render(&e, 3.0) == NULL); /* text stands still */
    box_engine_set_style(&e, BOX_STYLE_LED);
    const box_frame_t *led = box_engine_render(&e, 3.1);
    CHECK(led != NULL && lit_count(led) != smooth_lit); /* redrawn as LEDs */
    CHECK(box_engine_render(&e, 3.2) == NULL);
    box_engine_free(&e);
}

static void test_a_scene_can_fix_its_own_style(void)
{
    box_engine_t e;
    box_engine_init(&e, 48, 48, 16);
    box_scene_t *s = text_scene("", "HI");
    box_scene_set_style(s, BOX_STYLE_LED);
    box_engine_set_scene(&e, s, 0.0);
    const box_frame_t *f = box_engine_render(&e, 1.0);
    /* LED blocks are 3x3 on a 48 px screen with a 16 LED grid. */
    bool blocky = true;
    for (int y = 0; y < 48; y += 3) {
        for (int x = 0; x < 48; x += 3) {
            const box_rgb_t c = px(f, x, y);
            blocky &= px(f, x + 2, y + 2).r == c.r && px(f, x + 1, y).r == c.r;
        }
    }
    CHECK(blocky && lit_count(f) > 0);
    box_scene_release(s);
    box_engine_free(&e);
}

void run_render_tests(void)
{
    RUN(test_hex_roundtrip);
    RUN(test_frame_clear_set_and_bounds);
    RUN(test_led_text_wraps_whole_words_and_fits);
    RUN(test_led_text_puts_the_title_above_dimmer);
    RUN(test_font_and_utf8);
    RUN(test_images_play_loop_and_hold);
    RUN(test_images_fit_the_screen);
    RUN(test_ragged_images_and_the_budget);
    RUN(test_engine_draws_only_when_something_changes);
    RUN(test_a_scene_can_fix_its_own_style);
}
