// The LED look: grid geometry and dirty tracking.
#include "box/led.h"
#include "check.h"

#define SCREEN 480

static uint16_t at(const box_led_t *led, int x, int y)
{
    return box_led_pixels(led)[y * SCREEN + x];
}

static void test_the_grid_fills_the_screen(void)
{
    box_led_t *led = box_led_new(96, 96, SCREEN, SCREEN);
    CHECK(led != NULL && box_led_pitch(led) == 5);
    box_frame_t f;
    box_frame_init(&f, 96, 96);
    box_frame_clear(&f, (box_rgb_t){255, 255, 255});
    box_rect_t dirty;
    CHECK(box_led_update(led, &f, &dirty));
    CHECK(dirty.x1 == 0 && dirty.y1 == 0 && dirty.x2 == SCREEN - 1 && dirty.y2 == SCREEN - 1);
    /* Square screen: the corner dots are drawn, centred 2 px in. */
    CHECK(at(led, 2, 2) == 0xFFFF && at(led, 477, 477) == 0xFFFF);
    /* Unchanged: nothing to push. */
    CHECK(!box_led_update(led, &f, &dirty));
    box_frame_free(&f);
    box_led_free(led);
}

static void test_one_pixel_dirties_one_cell(void)
{
    box_led_t *led = box_led_new(32, 32, SCREEN, SCREEN);
    CHECK(box_led_pitch(led) == 15);
    box_frame_t f;
    box_frame_init(&f, 32, 32);
    box_rect_t dirty;
    box_led_update(led, &f, &dirty);
    box_frame_set_px(&f, 16, 16, (box_rgb_t){255, 0, 0});
    CHECK(box_led_update(led, &f, &dirty));
    /* Cell 16 spans 16 * 15 = 240 .. 254. */
    CHECK(dirty.x1 == 240 && dirty.x2 == 254 && dirty.y1 == 240 && dirty.y2 == 254);
    /* The dot's centre is lit red (green and blue only carry the unlit glow);
     * the gap between dots stays black. */
    const uint16_t centre = at(led, 247, 247);
    CHECK((centre >> 11) > 28 && ((centre >> 5) & 0x3F) < 8 && (centre & 0x1F) < 4);
    CHECK(at(led, 240, 240) == 0);
    box_frame_free(&f);
    box_led_free(led);
}

static void test_unlit_dots_glow_faintly(void)
{
    box_led_t *led = box_led_new(32, 32, SCREEN, SCREEN);
    box_frame_t f;
    box_frame_init(&f, 32, 32);
    box_rect_t dirty;
    box_led_update(led, &f, &dirty);
    const uint16_t dark = at(led, 247, 247);
    CHECK(dark != 0 && (dark >> 11) < 4);
    box_frame_free(&f);
    box_led_free(led);
}

static void test_a_panel_too_dense_for_the_screen_is_refused(void)
{
    CHECK(box_led_new(300, 300, SCREEN, SCREEN) == NULL);
}

void run_led_tests(void)
{
    RUN(test_the_grid_fills_the_screen);
    RUN(test_one_pixel_dirties_one_cell);
    RUN(test_unlit_dots_glow_faintly);
    RUN(test_a_panel_too_dense_for_the_screen_is_refused);
}
