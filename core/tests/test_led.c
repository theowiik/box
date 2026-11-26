// The led style's square blocks, and smooth text with Inter.
#include "box/font.h"
#include "box/led.h"
#include "box/text.h"
#include "check.h"

static bool lit(const box_frame_t *f, int x, int y)
{
    const uint8_t *p = f->pixels + (y * f->width + x) * 3;
    return p[0] || p[1] || p[2];
}

static void test_led_blocks_fill_the_screen(void)
{
    box_frame_t grid, screen;
    box_frame_init(&grid, 96, 96);
    box_frame_init(&screen, 480, 480);
    box_frame_set_px(&grid, 0, 0, (box_rgb_t){255, 0, 0});
    box_frame_set_px(&grid, 95, 95, (box_rgb_t){0, 0, 255});
    box_led_render(&grid, &screen);
    /* 5 px squares: (0,0) covers 0..4, nothing at 5. */
    CHECK(lit(&screen, 0, 0) && lit(&screen, 4, 4) && !lit(&screen, 5, 0) && !lit(&screen, 0, 5));
    CHECK(lit(&screen, 479, 479) && lit(&screen, 475, 475) && !lit(&screen, 474, 479));
    box_frame_free(&grid);
    box_frame_free(&screen);
}

static void test_a_grid_that_does_not_divide_is_centred(void)
{
    box_frame_t grid, screen;
    box_frame_init(&grid, 7, 7);
    box_frame_init(&screen, 50, 50); /* 7 px blocks: 49 px, half a pixel each side */
    box_frame_clear(&grid, (box_rgb_t){255, 255, 255});
    box_led_render(&grid, &screen);
    CHECK(lit(&screen, 0, 0) && lit(&screen, 48, 48) && !lit(&screen, 49, 49));
    box_frame_free(&grid);
    box_frame_free(&screen);
}

static box_card_layout_t card(const char *title, const char *body, int size)
{
    static uint32_t t[100], b[600];
    const size_t tn = box_utf8_decode(title, strlen(title), t, 100);
    const size_t bn = box_utf8_decode(body, strlen(body), b, 600);
    box_card_layout_t l;
    box_card_layout(t, tn, b, bn, size, size, &l);
    return l;
}

static void test_short_text_is_big_and_long_text_small(void)
{
    const box_card_layout_t hi = card("", "Hi", 480);
    CHECK(hi.body_lines == 1 && hi.body_size > 150 && !hi.truncated);
    const box_card_layout_t para = card(
        "", "The quick brown fox jumps over the lazy dog and then runs all the way home "
            "before it gets dark outside again, which it always does.", 480);
    CHECK(para.body_lines >= 4 && para.body_size < 60 && !para.truncated);
}

static void test_words_stay_whole_until_nothing_else_fits(void)
{
    const box_card_layout_t l = card("", "Unbelievably", 480);
    CHECK(l.body_lines == 1);
    const box_card_layout_t url = card("", "https://example.com/a/very/long/path/that/never/ends/at/all/because/why/not", 480);
    CHECK(url.body_lines >= 2 && !url.truncated);
}

static void test_a_title_sits_above_a_smaller_body(void)
{
    const box_card_layout_t l = card("Discord", "@theo mentioned you", 480);
    CHECK(l.title_lines == 1 && l.body_lines >= 1);
    CHECK(l.title_size >= 20 && l.title_size < l.body_size);
}

static void test_far_too_much_text_is_cut_with_an_ellipsis(void)
{
    char text[600];
    for (int i = 0; i < 100; ++i) {
        memcpy(text + i * 5, "word ", 5);
    }
    text[500] = '\0';
    const box_card_layout_t l = card("", text, 240);
    CHECK(l.truncated && l.body_size <= 20);
}

static void test_smooth_text_is_antialiased(void)
{
    box_frame_t f;
    box_frame_init(&f, 200, 100);
    const uint32_t text[] = {'O', 'k'};
    box_font_draw(&f, BOX_FONT_BOLD, 60, text, 2, 10, 70, (box_rgb_t){255, 255, 255});
    int full = 0, partial = 0;
    for (size_t i = 0; i < box_frame_bytes(&f); i += 3) {
        full += f.pixels[i] == 255;
        partial += f.pixels[i] > 0 && f.pixels[i] < 255;
    }
    CHECK(full > 100 && partial > 20);
    CHECK(box_font_width(BOX_FONT_BOLD, 60, text, 2) > 50);
    box_frame_free(&f);
}

void run_led_tests(void)
{
    RUN(test_led_blocks_fill_the_screen);
    RUN(test_a_grid_that_does_not_divide_is_centred);
    RUN(test_short_text_is_big_and_long_text_small);
    RUN(test_words_stay_whole_until_nothing_else_fits);
    RUN(test_a_title_sits_above_a_smaller_body);
    RUN(test_far_too_much_text_is_cut_with_an_ellipsis);
    RUN(test_smooth_text_is_antialiased);
}
