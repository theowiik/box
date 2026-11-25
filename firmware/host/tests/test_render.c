// Frames, the font, fitting text, scenes and the engine.
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

static bool column_lit(const box_frame_t *f, int x)
{
    for (int y = 0; y < f->height; ++y) {
        if (lit(f, x, y)) {
            return true;
        }
    }
    return false;
}

static bool frames_equal(const box_frame_t *a, const box_frame_t *b)
{
    return memcmp(a->pixels, b->pixels, box_frame_bytes(a)) == 0;
}

static box_scene_t *text_scene(const char *text, box_rgb_t color)
{
    return box_scene_new_text(text, strlen(text), color);
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

static box_text_layout_t layout_of(const char *text, int size)
{
    uint32_t cps[600];
    const size_t n = box_utf8_decode(text, strlen(text), cps, 600);
    box_text_layout_t layout;
    box_text_layout(cps, n, size, size, &layout);
    return layout;
}

static void test_short_text_is_big_centred_and_still(void)
{
    box_frame_t a, b;
    box_frame_init(&a, 96, 96);
    box_frame_init(&b, 96, 96);
    box_scene_t *s = text_scene("HI", WHITE);
    box_scene_render(s, &a, 0.0);
    box_scene_render(s, &b, 30.0);
    CHECK(frames_equal(&a, &b)); /* text never moves */
    /* Two glyphs at scale 8 are 88 dots wide, starting at column 4. H fills
     * its first column; I (01110) lights columns 60..83 of its 52..91 cell. */
    CHECK(layout_of("HI", 96).scale == 8);
    CHECK(!column_lit(&a, 3) && column_lit(&a, 4));
    CHECK(column_lit(&a, 60) && column_lit(&a, 83) && !column_lit(&a, 84));
    box_scene_release(s);
    box_frame_free(&a);
    box_frame_free(&b);
}

static void test_text_block_is_vertically_centred(void)
{
    box_frame_t f;
    box_frame_init(&f, 32, 32);
    box_scene_t *s = text_scene("H", WHITE);
    box_scene_render(s, &f, 0.0);
    /* One glyph fits 4x on 28 usable rows: 28 rows tall, centred in 32. */
    int top = -1, bottom = -1;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (lit(&f, x, y)) {
                top = top < 0 ? y : top;
                bottom = y;
            }
        }
    }
    CHECK(top == 2 && bottom == 29);
    box_scene_release(s);
    box_frame_free(&f);
}

static void test_words_wrap_whole_before_they_split(void)
{
    /* At scale 4 SUNNY would need splitting; scale 3 keeps it whole. */
    const box_text_layout_t l = layout_of("22°C SUNNY", 96);
    CHECK(l.scale == 3 && l.line_count == 2 && !l.truncated);
    CHECK(l.lines[0].count == 4 && l.lines[1].count == 5);
}

static void test_newline_forces_a_break(void)
{
    const box_text_layout_t l = layout_of("box.local\n192.168.0.228", 96);
    CHECK(l.line_count == 2 && l.lines[0].count == 9 && l.lines[1].count == 13);
}

static void test_a_long_word_is_split_only_when_nothing_else_fits(void)
{
    /* No size keeps 26 letters whole, so it splits — at the largest size
     * that then fits: scale 2, 7 columns. */
    const box_text_layout_t l = layout_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 96);
    CHECK(l.scale == 2 && l.line_count == 4 && !l.truncated);
    CHECK(l.lines[0].count == 7 && l.lines[3].count == 5);
}

static void test_a_paragraph_fits_small_without_scrolling(void)
{
    const char *text = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG AND THEN "
                       "RUNS ALL THE WAY HOME BEFORE IT GETS DARK OUTSIDE AGAIN";
    const box_text_layout_t l = layout_of(text, 96);
    CHECK(l.scale == 1 && !l.truncated && l.line_count <= 11);
}

static void test_text_too_long_for_any_size_is_cut_with_an_ellipsis(void)
{
    char text[520];
    for (int i = 0; i < 100; ++i) {
        memcpy(text + i * 5, "WORD ", 5);
    }
    text[500] = '\0';
    const box_text_layout_t l = layout_of(text, 96);
    CHECK(l.truncated && l.scale == 1 && l.line_count == 11);
    CHECK(l.lines[10].count <= 12); /* room left for "..." in 15 columns */
    box_frame_t f;
    box_frame_init(&f, 96, 96);
    box_scene_t *s = text_scene(text, WHITE);
    box_scene_render(s, &f, 0.0); /* draws without overrunning anything */
    box_scene_release(s);
    box_frame_free(&f);
}

static void test_blank_and_solid(void)
{
    box_frame_t f;
    box_frame_init(&f, 8, 8);
    box_frame_clear(&f, WHITE);
    box_scene_t *blank = box_scene_new_blank();
    box_scene_render(blank, &f, 0);
    CHECK(!lit(&f, 3, 3));
    box_scene_t *solid = box_scene_new_solid((box_rgb_t){10, 20, 30});
    box_scene_render(solid, &f, 0);
    CHECK(px(&f, 7, 7).g == 20);
    CHECK_STR(box_scene_kind_name(solid), "solid");
    CHECK(box_scene_text(solid) == NULL);
    box_scene_release(blank);
    box_scene_release(solid);
    box_frame_free(&f);
}

/* Three 2x2 frames, each a flat grey of 10, 20, 30. */
static box_scene_t *three_frames(float fps, bool loop)
{
    const size_t size = 3 * 2 * 2 * 3;
    uint8_t *data = malloc(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = (uint8_t)(10 * (1 + i / 12));
    }
    return box_scene_new_frames(data, size, 2, 2, fps, loop);
}

static void test_frames_play_loop_and_hold(void)
{
    box_frame_t f;
    box_frame_init(&f, 2, 2);
    box_scene_t *looping = three_frames(10.0f, true);
    box_scene_render(looping, &f, 0.0);
    CHECK(px(&f, 0, 0).r == 10);
    box_scene_render(looping, &f, 0.15);
    CHECK(px(&f, 0, 0).r == 20);
    box_scene_render(looping, &f, 0.35); /* frame 3 wraps to frame 0 */
    CHECK(px(&f, 0, 0).r == 10);
    box_scene_t *once = three_frames(10.0f, false);
    box_scene_render(once, &f, 5.0);
    CHECK(px(&f, 1, 1).r == 30);
    box_scene_release(looping);
    box_scene_release(once);
    box_frame_free(&f);
}

static void test_frames_scale_to_the_panel(void)
{
    /* One 2x1 frame (red, blue) shown on a 4x2 panel: left half red. */
    uint8_t *data = malloc(6);
    const uint8_t pixels[6] = {255, 0, 0, 0, 0, 255};
    memcpy(data, pixels, 6);
    box_scene_t *s = box_scene_new_frames(data, 6, 2, 1, 1.0f, true);
    box_frame_t f;
    box_frame_init(&f, 4, 2);
    box_scene_render(s, &f, 0);
    CHECK(px(&f, 0, 0).r == 255 && px(&f, 1, 1).r == 255);
    CHECK(px(&f, 2, 0).b == 255 && px(&f, 3, 1).b == 255);
    box_scene_release(s);
    box_frame_free(&f);
}

static void test_frames_reject_a_ragged_buffer(void)
{
    const size_t before = box_frames_in_use();
    CHECK(box_scene_new_frames(malloc(13), 13, 2, 2, 1.0f, true) == NULL);
    CHECK(box_frames_in_use() == before);
}

static void test_frames_count_against_the_budget(void)
{
    const size_t before = box_frames_in_use();
    box_scene_t *s = three_frames(1.0f, true);
    CHECK(box_frames_in_use() == before + 36);
    box_frames_set_budget(before + 40);
    CHECK(box_frames_budget_allows(4) && !box_frames_budget_allows(5));
    box_scene_release(s);
    CHECK(box_frames_in_use() == before);
    box_frames_set_budget(12u * 1024u * 1024u);
}

static void test_font(void)
{
    const uint8_t *fallback = box_font_glyph('?');
    CHECK(box_font_glyph(0x00C5) != fallback); /* Å */
    CHECK(box_font_glyph(0x00E5) == box_font_glyph(0x00C5)); /* å -> Å */
    CHECK(box_font_glyph(0x00F6) == box_font_glyph(0x00D6)); /* ö -> Ö */
    CHECK(box_font_glyph('a') == box_font_glyph('A'));
    CHECK(box_font_glyph(0x2603) == fallback); /* snowman */
    CHECK(box_text_width(0, 1) == 0 && box_text_width(3, 1) == 17);
    CHECK(box_text_width(2, 7) == 77);
}

static void test_utf8(void)
{
    uint32_t cps[8];
    const char *s = "Å\xFFo"; /* valid, malformed, valid */
    CHECK(box_utf8_count(s, strlen(s)) == 3);
    CHECK(box_utf8_decode(s, strlen(s), cps, 8) == 3);
    CHECK(cps[0] == 0xC5 && cps[1] == 0xFFFD && cps[2] == 'o');
    CHECK(box_utf8_count("\xC3", 1) == 1); /* truncated sequence */
}

static void test_engine_crossfades_then_settles(void)
{
    box_engine_t e;
    CHECK(box_engine_init(&e, 2, 2));
    box_scene_t *white = box_scene_new_solid(WHITE);
    box_engine_set_scene(&e, white, 10.0);
    box_scene_release(white); /* the engine holds its own reference */
    const box_frame_t *f = box_engine_render(&e, 10.0 + BOX_TRANSITION / 2);
    CHECK(px(f, 0, 0).r > 100 && px(f, 0, 0).r < 155);
    f = box_engine_render(&e, 10.0 + BOX_TRANSITION + 0.01);
    CHECK(px(f, 0, 0).r == 255);
    CHECK(e.prev == NULL);
    box_engine_free(&e);
}

void run_render_tests(void)
{
    RUN(test_hex_roundtrip);
    RUN(test_frame_clear_set_and_bounds);
    RUN(test_short_text_is_big_centred_and_still);
    RUN(test_text_block_is_vertically_centred);
    RUN(test_words_wrap_whole_before_they_split);
    RUN(test_newline_forces_a_break);
    RUN(test_a_long_word_is_split_only_when_nothing_else_fits);
    RUN(test_a_paragraph_fits_small_without_scrolling);
    RUN(test_text_too_long_for_any_size_is_cut_with_an_ellipsis);
    RUN(test_blank_and_solid);
    RUN(test_frames_play_loop_and_hold);
    RUN(test_frames_scale_to_the_panel);
    RUN(test_frames_reject_a_ragged_buffer);
    RUN(test_frames_count_against_the_budget);
    RUN(test_font);
    RUN(test_utf8);
    RUN(test_engine_crossfades_then_settles);
}
