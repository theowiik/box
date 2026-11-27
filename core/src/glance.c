// The glance: the time, the weather and a smiley on one screen.
//
// Drawn from shapes and text, never from pixels a sender chose: a sender
// says the time, the temperature, what the sky is doing and the mood, and
// this lays it out. Static, so a still glance costs nothing.
#include "glance.h"

#include <math.h>
#include <string.h>

#include "box/font.h"
#include "box/text.h"

const char *const BOX_GLANCE_SKIES[] = {"sun",  "moon", "partly", "cloud", "fog",
                                        "rain", "snow", "storm",  NULL};
const char *const BOX_GLANCE_MOODS[] = {"happy", "good", "meh", "sad", NULL};

static const box_rgb_t WHITE = {255, 255, 255};
static const box_rgb_t MUTED = {150, 158, 170};
static const box_rgb_t SUN = {255, 200, 60};
static const box_rgb_t MOON = {225, 230, 245};
static const box_rgb_t CLOUD = {215, 222, 232};
static const box_rgb_t DARK_CLOUD = {120, 130, 145};
static const box_rgb_t RAIN = {90, 170, 255};
static const box_rgb_t BOLT = {255, 220, 70};
static const box_rgb_t FACE = {255, 212, 70};
static const box_rgb_t FEATURES = {70, 45, 20};

/* --- shapes: coverage from distance, so edges are antialiased ------------- */

static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static void disc(box_frame_t *f, float cx, float cy, float r, box_rgb_t c)
{
    for (int y = (int)floorf(cy - r - 1); y <= (int)ceilf(cy + r + 1); ++y) {
        for (int x = (int)floorf(cx - r - 1); x <= (int)ceilf(cx + r + 1); ++x) {
            const float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            const float a = clamp01(r - d + 0.5f);
            if (a > 0) {
                box_frame_blend_px(f, x, y, c, a);
            }
        }
    }
}

/* A line with round ends, `w` wide. */
static void stroke(box_frame_t *f, float x0, float y0, float x1, float y1, float w, box_rgb_t c)
{
    const float r = w / 2, dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = (int)floorf(fminf(y0, y1) - r - 1); y <= (int)ceilf(fmaxf(y0, y1) + r + 1); ++y) {
        for (int x = (int)floorf(fminf(x0, x1) - r - 1); x <= (int)ceilf(fmaxf(x0, x1) + r + 1);
             ++x) {
            const float px = x + 0.5f - x0, py = y + 0.5f - y0;
            const float t = len2 > 0 ? clamp01((px * dx + py * dy) / len2) : 0;
            const float d = hypotf(px - t * dx, py - t * dy);
            const float a = clamp01(r - d + 0.5f);
            if (a > 0) {
                box_frame_blend_px(f, x, y, c, a);
            }
        }
    }
}

/* An arc of radius r from angle a0 to a1 (radians, y down), `w` wide, round ends. */
static void arc(box_frame_t *f, float cx, float cy, float r, float a0, float a1, float w,
                box_rgb_t c)
{
    const int steps = 24;
    for (int i = 0; i < steps; ++i) {
        const float t0 = a0 + (a1 - a0) * i / steps, t1 = a0 + (a1 - a0) * (i + 1) / steps;
        /* Segments overlap at their joints; blend once per pixel by drawing
         * opaque colours only (every caller does). */
        stroke(f, cx + r * cosf(t0), cy + r * sinf(t0), cx + r * cosf(t1), cy + r * sinf(t1), w,
               c);
    }
}

static void round_rect(box_frame_t *f, float x0, float y0, float w, float h, float r,
                       box_rgb_t c, float alpha)
{
    for (int y = (int)floorf(y0); y < (int)ceilf(y0 + h); ++y) {
        for (int x = (int)floorf(x0); x < (int)ceilf(x0 + w); ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float qx = fmaxf(fabsf(px - (x0 + w / 2)) - (w / 2 - r), 0);
            const float qy = fmaxf(fabsf(py - (y0 + h / 2)) - (h / 2 - r), 0);
            const float a = clamp01(r - hypotf(qx, qy) + 0.5f);
            if (a > 0) {
                box_frame_blend_px(f, x, y, c, a * alpha);
            }
        }
    }
}

static box_rgb_t scale_rgb(box_rgb_t c, float k)
{
    return (box_rgb_t){(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
}

/* --- the pieces ------------------------------------------------------------ */

static void cloud(box_frame_t *f, float cx, float cy, float r, box_rgb_t c)
{
    disc(f, cx - r * 0.45f, cy + r * 0.12f, r * 0.42f, c);
    disc(f, cx + r * 0.1f, cy - r * 0.15f, r * 0.55f, c);
    disc(f, cx + r * 0.55f, cy + r * 0.15f, r * 0.38f, c);
    round_rect(f, cx - r * 0.85f, cy + r * 0.05f, r * 1.78f, r * 0.5f, r * 0.25f, c, 1);
}

static void sun(box_frame_t *f, float cx, float cy, float r)
{
    for (int i = 0; i < 8; ++i) {
        const float a = (float)M_PI * i / 4;
        stroke(f, cx + cosf(a) * r * 0.72f, cy + sinf(a) * r * 0.72f, cx + cosf(a) * r * 0.98f,
               cy + sinf(a) * r * 0.98f, r * 0.13f, SUN);
    }
    disc(f, cx, cy, r * 0.5f, SUN);
}

static void moon(box_frame_t *f, float cx, float cy, float r)
{
    /* The bite is whatever is behind the moon. */
    const int bx = (int)(cx + r * 0.32f), by = (int)(cy - r * 0.22f);
    box_rgb_t bg = {0, 0, 0};
    if (bx >= 0 && by >= 0 && bx < f->width && by < f->height) {
        const uint8_t *p = f->pixels + ((size_t)by * f->width + bx) * 3;
        bg = (box_rgb_t){p[0], p[1], p[2]};
    }
    disc(f, cx, cy, r * 0.62f, MOON);
    disc(f, cx + r * 0.32f, cy - r * 0.22f, r * 0.5f, bg);
}

static void weather_icon(box_frame_t *f, box_glance_sky_t sky, float cx, float cy, float r)
{
    switch (sky) {
    case BOX_SKY_SUN:
        sun(f, cx, cy, r);
        break;
    case BOX_SKY_MOON:
        moon(f, cx, cy, r);
        break;
    case BOX_SKY_PARTLY:
        sun(f, cx - r * 0.3f, cy - r * 0.3f, r * 0.7f);
        cloud(f, cx + r * 0.1f, cy + r * 0.15f, r * 0.8f, CLOUD);
        break;
    case BOX_SKY_CLOUD:
        cloud(f, cx, cy, r, CLOUD);
        break;
    case BOX_SKY_FOG:
        for (int i = 0; i < 3; ++i) {
            const float y = cy - r * 0.45f + i * r * 0.45f, inset = (i % 2) * r * 0.25f;
            stroke(f, cx - r * 0.8f + inset, y, cx + r * 0.8f - inset, y, r * 0.18f, CLOUD);
        }
        break;
    case BOX_SKY_RAIN:
        cloud(f, cx, cy - r * 0.25f, r * 0.85f, DARK_CLOUD);
        for (int i = -1; i <= 1; ++i) {
            const float x = cx + i * r * 0.4f;
            stroke(f, x + r * 0.08f, cy + r * 0.45f, x - r * 0.08f, cy + r * 0.8f, r * 0.12f, RAIN);
        }
        break;
    case BOX_SKY_SNOW:
        cloud(f, cx, cy - r * 0.25f, r * 0.85f, CLOUD);
        for (int i = -1; i <= 1; ++i) {
            disc(f, cx + i * r * 0.4f, cy + r * (i ? 0.55f : 0.75f), r * 0.1f, WHITE);
        }
        break;
    case BOX_SKY_STORM:
    default:
        cloud(f, cx, cy - r * 0.25f, r * 0.85f, DARK_CLOUD);
        stroke(f, cx + r * 0.1f, cy + r * 0.2f, cx - r * 0.15f, cy + r * 0.55f, r * 0.13f, BOLT);
        stroke(f, cx - r * 0.15f, cy + r * 0.55f, cx + r * 0.12f, cy + r * 0.55f, r * 0.13f, BOLT);
        stroke(f, cx + r * 0.12f, cy + r * 0.55f, cx - r * 0.1f, cy + r * 0.95f, r * 0.13f, BOLT);
        break;
    }
}

static void smiley(box_frame_t *f, box_glance_mood_t mood, float cx, float cy, float r)
{
    disc(f, cx, cy, r, FACE);
    const float eye = r * 0.12f, w = r * 0.13f;
    disc(f, cx - r * 0.33f, cy - r * 0.2f, eye, FEATURES);
    disc(f, cx + r * 0.33f, cy - r * 0.2f, eye, FEATURES);
    const float pi = (float)M_PI;
    switch (mood) {
    case BOX_MOOD_HAPPY:
        arc(f, cx, cy + r * 0.02f, r * 0.5f, pi * 0.15f, pi * 0.85f, w, FEATURES);
        break;
    case BOX_MOOD_GOOD:
        arc(f, cx, cy - r * 0.1f, r * 0.5f, pi * 0.3f, pi * 0.7f, w, FEATURES);
        break;
    case BOX_MOOD_MEH:
        stroke(f, cx - r * 0.3f, cy + r * 0.38f, cx + r * 0.3f, cy + r * 0.38f, w, FEATURES);
        break;
    case BOX_MOOD_SAD:
    default:
        arc(f, cx, cy + r * 0.75f, r * 0.42f, pi * 1.25f, pi * 1.75f, w, FEATURES);
        break;
    }
}

/* Text at `size`, shrunk until it is at most `max_w` wide. */
static float fit_size(box_font_face_t face, float size, const uint32_t *cps, size_t n,
                      float max_w)
{
    const int w = box_font_width(face, 100, cps, n);
    return w > 0 ? fminf(size, max_w * 100 / w) : size;
}

/* --- smooth ---------------------------------------------------------------- */

void box_glance_draw_smooth(box_frame_t *f, const box_glance_t *g)
{
    const float S = (float)(f->width < f->height ? f->width : f->height), u = S / 100;
    const float ox = (f->width - S) / 2, oy = (f->height - S) / 2;
    /* A dark sky tinted by the weather's colour, fading to night below. */
    const box_rgb_t top = scale_rgb(g->color, 0.28f), bottom = {8, 10, 16};
    for (int y = 0; y < f->height; ++y) {
        const float t = clamp01((float)y / f->height);
        const box_rgb_t c = {(uint8_t)(top.r + (bottom.r - top.r) * t),
                             (uint8_t)(top.g + (bottom.g - top.g) * t),
                             (uint8_t)(top.b + (bottom.b - top.b) * t)};
        for (int x = 0; x < f->width; ++x) {
            box_frame_set_px(f, x, y, c);
        }
    }

    const float left = ox + 8 * u, width = 84 * u;
    float size = fit_size(BOX_FONT_MEDIUM, 6.5f * u, g->date.cps, g->date.count, width);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->date.cps, g->date.count, (int)left,
                  (int)(oy + 14 * u), scale_rgb(WHITE, 0.72f));
    size = fit_size(BOX_FONT_BOLD, 36 * u, g->time.cps, g->time.count, width);
    box_font_draw(f, BOX_FONT_BOLD, size, g->time.cps, g->time.count, (int)(left - 1.2f * u),
                  (int)(oy + 47 * u), WHITE);

    /* The weather card: icon, temperature and condition, and the smiley. */
    const float cy = oy + 76 * u;
    round_rect(f, ox + 5 * u, oy + 57 * u, 90 * u, 38 * u, 7 * u, WHITE, 0.08f);
    weather_icon(f, g->sky, ox + 20 * u, cy, 10.5f * u);
    const float text_x = ox + 36 * u, text_w = 30 * u;
    size = fit_size(BOX_FONT_BOLD, 15 * u, g->temp.cps, g->temp.count, text_w);
    box_font_draw(f, BOX_FONT_BOLD, size, g->temp.cps, g->temp.count, (int)text_x,
                  (int)(cy + 3 * u), WHITE);
    size = fit_size(BOX_FONT_MEDIUM, 5.2f * u, g->condition.cps, g->condition.count, text_w);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->condition.cps, g->condition.count, (int)text_x,
                  (int)(cy + 12 * u), MUTED);
    smiley(f, g->mood, ox + 80 * u, cy, 10.5f * u);
}

/* --- led ------------------------------------------------------------------- */

void box_glance_draw_led(box_frame_t *f, const box_glance_t *g)
{
    box_frame_clear(f, (box_rgb_t){0, 0, 0});
    const int G = f->width < f->height ? f->width : f->height;
    const int ox = (f->width - G) / 2, oy = (f->height - G) / 2;
    int sc = (G - 2 * BOX_TEXT_MARGIN) / box_text_width(g->time.count ? g->time.count : 1, 1);
    sc = sc < 1 ? 1 : sc > 3 ? 3 : sc;
    const int tw = box_text_width(g->time.count, sc);
    box_frame_draw_text(f, g->time.cps, g->time.count, ox + (G - tw) / 2, oy + G * 14 / 100, sc,
                        WHITE);

    /* The temperature without the degree sign, which the bitmap font lacks;
     * a small dot stands in for it. */
    uint32_t temp[16];
    size_t n = 0;
    bool degree = false;
    for (size_t i = 0; i < g->temp.count && n < 16; ++i) {
        if (g->temp.cps[i] == 0xB0) {
            degree = true;
        } else {
            temp[n++] = g->temp.cps[i];
        }
    }
    const float r = G * 0.14f, cy = oy + G * 0.70f;
    weather_icon(f, g->sky, ox + G * 0.2f, cy, r);
    smiley(f, g->mood, ox + G * 0.8f, cy, r);
    const int ts = n * BOX_GLYPH_ADVANCE * 2 <= (size_t)(G * 0.32f) ? 2 : 1;
    const int w = box_text_width(n, ts) + (degree ? 3 * ts : 0);
    const int x = ox + (G - w) / 2, y = (int)cy - BOX_GLYPH_HEIGHT * ts / 2;
    box_frame_draw_text(f, temp, n, x, y, ts, WHITE);
    if (degree) {
        for (int dy = 0; dy < 2 * ts; ++dy) {
            for (int dx = 0; dx < 2 * ts; ++dx) {
                box_frame_set_px(f, x + w - 2 * ts + dx, y + dy, WHITE);
            }
        }
    }
}
