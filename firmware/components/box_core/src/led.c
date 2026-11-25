#include "box/led.h"

#include <stdlib.h>
#include <string.h>

/* Coverage levels for the antialiased dot edge. Per cell, each level maps to
 * one precomputed RGB565 colour, so drawing a dot is a table lookup per pixel. */
#define LEVELS 16
#define SUPERSAMPLE 4

struct box_led {
    int panel_w, panel_h;
    int screen_w, screen_h;
    int pitch;
    int origin_x, origin_y;
    uint8_t *mask;      /* pitch * pitch coverage levels, 0..LEVELS-1 */
    uint8_t *last;      /* the logical frame last drawn */
    bool drawn;         /* false until the first full draw */
    uint16_t *screen;   /* screen_w * screen_h RGB565 */
};

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void build_mask(box_led_t *led)
{
    const float radius = led->pitch * BOX_LED_DOT / 2.0f;
    const float centre = led->pitch / 2.0f;
    for (int y = 0; y < led->pitch; ++y) {
        for (int x = 0; x < led->pitch; ++x) {
            int inside = 0;
            for (int sy = 0; sy < SUPERSAMPLE; ++sy) {
                for (int sx = 0; sx < SUPERSAMPLE; ++sx) {
                    const float px = x + (sx + 0.5f) / SUPERSAMPLE - centre;
                    const float py = y + (sy + 0.5f) / SUPERSAMPLE - centre;
                    inside += px * px + py * py <= radius * radius;
                }
            }
            const int samples = SUPERSAMPLE * SUPERSAMPLE;
            led->mask[y * led->pitch + x] =
                (uint8_t)((inside * (LEVELS - 1) + samples / 2) / samples);
        }
    }
}

box_led_t *box_led_new(int panel_w, int panel_h, int screen_w, int screen_h)
{
    if (panel_w < 1 || panel_h < 1 || screen_w < 1 || screen_h < 1) {
        return NULL;
    }
    const int span = screen_w < screen_h ? screen_w : screen_h;
    const int pitch = span / (panel_w > panel_h ? panel_w : panel_h);
    if (pitch < 2) {
        return NULL;
    }
    box_led_t *led = calloc(1, sizeof(*led));
    if (!led) {
        return NULL;
    }
    led->panel_w = panel_w;
    led->panel_h = panel_h;
    led->screen_w = screen_w;
    led->screen_h = screen_h;
    led->pitch = pitch;
    led->origin_x = (screen_w - pitch * panel_w) / 2;
    led->origin_y = (screen_h - pitch * panel_h) / 2;
    led->mask = malloc((size_t)pitch * (size_t)pitch);
    led->last = malloc((size_t)panel_w * (size_t)panel_h * 3);
    led->screen = calloc((size_t)screen_w * (size_t)screen_h, sizeof(uint16_t));
    if (!led->mask || !led->last || !led->screen) {
        box_led_free(led);
        return NULL;
    }
    build_mask(led);
    return led;
}

void box_led_free(box_led_t *led)
{
    if (!led) {
        return;
    }
    free(led->mask);
    free(led->last);
    free(led->screen);
    free(led);
}

const uint16_t *box_led_pixels(const box_led_t *led)
{
    return led->screen;
}

int box_led_pitch(const box_led_t *led)
{
    return led->pitch;
}

static void draw_cell(box_led_t *led, int cx, int cy, const uint8_t *rgb)
{
    /* Lift the colour above the unlit glow, so black reads as a dark LED. */
    const unsigned span = 255 - BOX_LED_OFF;
    const unsigned r = BOX_LED_OFF + rgb[0] * span / 255;
    const unsigned g = BOX_LED_OFF + rgb[1] * span / 255;
    const unsigned b = BOX_LED_OFF + rgb[2] * span / 255;
    uint16_t palette[LEVELS];
    for (unsigned level = 0; level < LEVELS; ++level) {
        palette[level] = rgb565(r * level / (LEVELS - 1), g * level / (LEVELS - 1),
                                b * level / (LEVELS - 1));
    }
    const int x0 = led->origin_x + cx * led->pitch;
    const int y0 = led->origin_y + cy * led->pitch;
    for (int y = 0; y < led->pitch; ++y) {
        uint16_t *row = led->screen + (size_t)(y0 + y) * (size_t)led->screen_w + (size_t)x0;
        const uint8_t *mask = led->mask + y * led->pitch;
        for (int x = 0; x < led->pitch; ++x) {
            row[x] = palette[mask[x]];
        }
    }
}

bool box_led_update(box_led_t *led, const box_frame_t *frame, box_rect_t *dirty)
{
    if (frame->width != led->panel_w || frame->height != led->panel_h) {
        return false;
    }
    int min_x = led->panel_w, min_y = led->panel_h, max_x = -1, max_y = -1;
    for (int y = 0; y < led->panel_h; ++y) {
        for (int x = 0; x < led->panel_w; ++x) {
            const size_t cell = (size_t)y * (size_t)led->panel_w + (size_t)x;
            const uint8_t *now = frame->pixels + cell * 3;
            uint8_t *was = led->last + cell * 3;
            if (led->drawn && memcmp(now, was, 3) == 0) {
                continue;
            }
            memcpy(was, now, 3);
            draw_cell(led, x, y, now);
            min_x = x < min_x ? x : min_x;
            min_y = y < min_y ? y : min_y;
            max_x = x > max_x ? x : max_x;
            max_y = y > max_y ? y : max_y;
        }
    }
    const bool first = !led->drawn;
    led->drawn = true;
    if (first) {
        /* The first frame covers the whole screen, margins included. */
        *dirty = (box_rect_t){0, 0, led->screen_w - 1, led->screen_h - 1};
        return true;
    }
    if (max_x < 0) {
        return false;
    }
    dirty->x1 = led->origin_x + min_x * led->pitch;
    dirty->y1 = led->origin_y + min_y * led->pitch;
    dirty->x2 = led->origin_x + (max_x + 1) * led->pitch - 1;
    dirty->y2 = led->origin_y + (max_y + 1) * led->pitch - 1;
    return true;
}
