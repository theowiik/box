// The glance: the time, the weather and a smiley on one screen.
//
// Drawn from shapes and text, never from pixels a sender chose: a sender
// says the time, the temperature, what the sky is doing and the mood, and
// this lays it out. Static, so a still glance costs nothing.
#include "glance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "box/font.h"
#include "box/text.h"

const char *const BOX_GLANCE_SKIES[] = {"sun",  "moon", "partly", "cloud", "fog",
                                        "rain", "snow", "storm",  NULL};
const char *const BOX_GLANCE_MOODS[] = {"happy", "good", "meh", "sad", NULL};

static const box_rgb_t WHITE = {255, 255, 255};
static const box_rgb_t MUTED = {150, 158, 170};
static const box_rgb_t DIM = {120, 126, 136};
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

/* --- the dot clock: every lit dot of the time is a ball ------------------- */

/* The time is 5x7 glyphs of dots, in dot units: column, row. When it changes,
 * dots that stay lit stay; the others hop with gravity to where the new time
 * needs them, spare ones fall away, and missing ones drop in from above. */
#define COLS_MAX 32
#define ROWS 7
#define FLIGHT 0.65f   /* seconds a dot is in the air */
#define WAVE 0.025f    /* seconds between columns starting, left to right */
#define SETTLE 0.35f   /* seconds a landed dot bounces */
#define DOTS_MAX (COLS_MAX * ROWS * 2)

typedef enum { DOT_STAY, DOT_HOP, DOT_DROP, DOT_FALL } dot_kind_t;

typedef struct {
    float x0, y0, x1, y1, delay;
    dot_kind_t kind;
} dot_t;

struct box_glance_anim {
    uint32_t shown[8]; /* the time the dots make */
    size_t shown_count;
    bool any;
    double changed; /* scene time the dots last started moving; < 0 never */
    uint64_t changes;
    dot_t dots[DOTS_MAX];
    size_t count;
};

box_glance_anim_t *box_glance_anim_new(void)
{
    box_glance_anim_t *a = calloc(1, sizeof(*a));
    if (a) {
        a->changed = -1e9;
    }
    return a;
}

void box_glance_anim_free(box_glance_anim_t *a)
{
    free(a);
}

typedef struct {
    float x, y;
} pt_t;

static size_t dots_of(const uint32_t *cps, size_t n, pt_t *out)
{
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *rows = box_font_glyph(cps[i]);
        for (int y = 0; y < ROWS; ++y) {
            for (int x = 0; x < BOX_GLYPH_WIDTH; ++x) {
                if (rows[y] & (0x10 >> x)) {
                    out[count++] = (pt_t){(float)(i * BOX_GLYPH_ADVANCE + x), (float)y};
                }
            }
        }
    }
    return count;
}

static int by_x(const void *a, const void *b)
{
    const pt_t *p = a, *q = b;
    return p->x != q->x ? (p->x < q->x ? -1 : 1) : (p->y < q->y ? -1 : p->y > q->y);
}

static float hash01(uint32_t n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (n & 0xFFFF) / 65535.0f;
}

static bool contains(const pt_t *set, size_t n, pt_t p)
{
    for (size_t i = 0; i < n; ++i) {
        if (set[i].x == p.x && set[i].y == p.y) {
            return true;
        }
    }
    return false;
}

static void retarget(box_glance_anim_t *a, const uint32_t *cps, size_t n, double t)
{
    static pt_t old[DOTS_MAX], next[DOTS_MAX], gone[DOTS_MAX], fresh[DOTS_MAX];
    const size_t no = a->any ? dots_of(a->shown, a->shown_count, old) : 0;
    const size_t nn = dots_of(cps, n, next);
    size_t ng = 0, nf = 0;
    a->count = 0;
    for (size_t i = 0; i < nn; ++i) {
        if (!a->any || contains(old, no, next[i])) {
            a->dots[a->count++] = (dot_t){next[i].x, next[i].y, next[i].x, next[i].y, 0, DOT_STAY};
        } else {
            fresh[nf++] = next[i];
        }
    }
    for (size_t i = 0; i < no; ++i) {
        if (!contains(next, nn, old[i])) {
            gone[ng++] = old[i];
        }
    }
    qsort(gone, ng, sizeof(pt_t), by_x);
    qsort(fresh, nf, sizeof(pt_t), by_x);
    const size_t pairs = ng < nf ? ng : nf;
    for (size_t i = 0; i < pairs; ++i) {
        a->dots[a->count++] = (dot_t){gone[i].x, gone[i].y, fresh[i].x, fresh[i].y,
                                      WAVE * fresh[i].x, DOT_HOP};
    }
    for (size_t i = pairs; i < nf; ++i) { /* more needed: drop in from above */
        const float above = 4 + 6 * hash01((uint32_t)(i * 7 + (size_t)fresh[i].x));
        a->dots[a->count++] = (dot_t){fresh[i].x, -above, fresh[i].x, fresh[i].y,
                                      WAVE * fresh[i].x, DOT_DROP};
    }
    for (size_t i = pairs; i < ng; ++i) { /* spare: fall away */
        a->dots[a->count++] = (dot_t){gone[i].x, gone[i].y, gone[i].x, gone[i].y + 14,
                                      WAVE * gone[i].x, DOT_FALL};
    }
    memcpy(a->shown, cps, n * sizeof(uint32_t));
    a->shown_count = n;
    a->changed = a->any ? t : -1e9;
    a->any = true;
    ++a->changes;
}

/* Where a dot is `s` seconds after the change, and how lit (0..1). */
static pt_t dot_at(const dot_t *d, float s, float *alpha)
{
    *alpha = 1;
    if (d->kind == DOT_STAY) {
        return (pt_t){d->x1, d->y1};
    }
    const float p = clamp01((s - d->delay) / FLIGHT);
    pt_t at;
    switch (d->kind) {
    case DOT_HOP: { /* a throw under gravity: up, over, down */
        const float lift = 3 + fabsf(d->x1 - d->x0) * 0.15f;
        at.x = d->x0 + (d->x1 - d->x0) * p;
        at.y = d->y0 + (d->y1 - d->y0) * p - 4 * lift * p * (1 - p);
        break;
    }
    case DOT_DROP:
        at.x = d->x1;
        at.y = d->y0 + (d->y1 - d->y0) * p * p; /* accelerating */
        break;
    case DOT_FALL:
    default:
        at.x = d->x0;
        at.y = d->y0 + (d->y1 - d->y0) * p * p;
        *alpha = 1 - p;
        return at;
    }
    /* A landed dot bounces a little, less each time. */
    const float since = s - d->delay - FLIGHT;
    if (since > 0 && since < SETTLE) {
        const float k = since / SETTLE;
        at.y -= 0.8f * (1 - k) * fabsf(sinf(k * 3 * (float)M_PI));
    }
    return at;
}

static float anim_length(void)
{
    return WAVE * COLS_MAX + FLIGHT + SETTLE;
}

/* --- the smiley's life ------------------------------------------------------ */

/* It blinks now and then, looks around, and hops when the minute turns. */
#define BLINK_EVERY 5.3f
#define BLINK 0.18f
#define LOOK_EVERY 9.7f
#define LOOK 2.2f
#define LOOK_MOVE 0.25f
#define HOP 0.6f

typedef struct {
    float blink; /* 1 open, 0 shut */
    float look;  /* -1 left .. 1 right */
    float hop;   /* 0 resting .. 1 high */
    bool moving;
} face_t;

static face_t face_at(const box_glance_anim_t *a, double t)
{
    face_t f = {1, 0, 0, false};
    const float b = fmodf((float)t + 2.1f, BLINK_EVERY);
    if (b < BLINK) {
        f.blink = fabsf(1 - 2 * b / BLINK);
        f.moving = true;
    }
    const float l = fmodf((float)t, LOOK_EVERY);
    const int turn = (int)(t / LOOK_EVERY);
    const float dir = turn % 2 ? -1.0f : 1.0f;
    if (l < LOOK) {
        const float in = clamp01(l / LOOK_MOVE), out = clamp01((LOOK - l) / LOOK_MOVE);
        const float k = fminf(in, out);
        f.look = dir * k * k * (3 - 2 * k);
        f.moving |= l < LOOK_MOVE || l > LOOK - LOOK_MOVE;
    }
    const float h = (float)(t - a->changed) - 0.25f;
    if (h > 0 && h < HOP) {
        f.hop = sinf(h / HOP * (float)M_PI);
        f.moving = true;
    }
    return f;
}

uint64_t box_glance_anim_version(box_glance_anim_t *a, const uint32_t *time, size_t n, double t)
{
    if (n > 8) {
        n = 8;
    }
    if (!a->any || n != a->shown_count || memcmp(time, a->shown, n * sizeof(uint32_t))) {
        retarget(a, time, n, t);
    }
    const bool dots = t - a->changed < anim_length();
    const bool moving = dots || face_at(a, t).moving;
    /* A frame number while anything moves; otherwise still. */
    return (a->changes << 32) + (moving ? (uint64_t)(t * 30) + 1 : 0);
}

static void smiley(box_frame_t *f, box_glance_mood_t mood, float cx, float cy, float r, face_t m)
{
    cy -= m.hop * r * 0.35f;
    disc(f, cx, cy, r, FACE);
    const float pi = (float)M_PI, w = r * 0.13f;
    const float ex = r * 0.33f, ey = cy - r * 0.2f, er = r * 0.12f, shift = m.look * r * 0.14f;
    for (int side = -1; side <= 1; side += 2) {
        const float x = cx + side * ex + shift;
        if (m.blink > 0.35f) {
            /* An eye squashes as it shuts: a short stack of discs. */
            const float half = er * (m.blink - 0.35f) / 0.65f * 0.9f;
            stroke(f, x, ey - half, x, ey + half, er * 2, FEATURES);
        } else {
            stroke(f, x - er, ey, x + er, ey, w * 0.8f, FEATURES);
        }
    }
    const float mx = cx + shift * 0.5f;
    switch (mood) {
    case BOX_MOOD_HAPPY:
        arc(f, mx, cy + r * 0.02f, r * 0.5f, pi * 0.15f, pi * 0.85f, w, FEATURES);
        break;
    case BOX_MOOD_GOOD:
        arc(f, mx, cy - r * 0.1f, r * 0.5f, pi * 0.3f, pi * 0.7f, w, FEATURES);
        break;
    case BOX_MOOD_MEH:
        stroke(f, mx - r * 0.3f, cy + r * 0.38f, mx + r * 0.3f, cy + r * 0.38f, w, FEATURES);
        break;
    case BOX_MOOD_SAD:
    default:
        arc(f, mx, cy + r * 0.75f, r * 0.42f, pi * 1.25f, pi * 1.75f, w, FEATURES);
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

/* Draw the dots with the clock's top-left at (x, y), `pitch` apart. Smooth
 * draws round dots; led draws square blocks. */
static void draw_dots(box_frame_t *f, const box_glance_anim_t *a, double t, float x, float y,
                      float pitch, bool round)
{
    const float s = (float)(t - a->changed);
    for (size_t i = 0; i < a->count; ++i) {
        float alpha;
        const pt_t p = dot_at(&a->dots[i], s, &alpha);
        if (alpha <= 0.02f) {
            continue;
        }
        const box_rgb_t c = a->dots[i].kind == DOT_FALL ? scale_rgb(DIM, alpha) : WHITE;
        if (round) {
            disc(f, x + (p.x + 0.5f) * pitch, y + (p.y + 0.5f) * pitch, pitch * 0.42f, c);
        } else {
            const int px = (int)lroundf(x + p.x * pitch), py = (int)lroundf(y + p.y * pitch);
            for (int dy = 0; dy < (int)pitch; ++dy) {
                for (int dx = 0; dx < (int)pitch; ++dx) {
                    box_frame_set_px(f, px + dx, py + dy, c);
                }
            }
        }
    }
}

static int columns(const box_glance_anim_t *a)
{
    return a->shown_count ? box_text_width(a->shown_count, 1) : 1;
}

/* --- smooth ---------------------------------------------------------------- */

void box_glance_draw_smooth(box_frame_t *f, const box_glance_t *g, const box_glance_anim_t *a,
                            double t)
{
    box_frame_clear(f, (box_rgb_t){0, 0, 0}); /* an OLED: black is off */
    const float S = (float)(f->width < f->height ? f->width : f->height), u = S / 100;
    const float ox = (f->width - S) / 2, oy = (f->height - S) / 2;

    const float left = ox + 8 * u, width = 84 * u;
    float size = fit_size(BOX_FONT_MEDIUM, 6.5f * u, g->date.cps, g->date.count, width);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->date.cps, g->date.count, (int)left,
                  (int)(oy + 14 * u), scale_rgb(WHITE, 0.6f));
    const float pitch = width / columns(a);
    draw_dots(f, a, t, left, oy + 22 * u, pitch, true);

    const float cy = oy + 76 * u;
    weather_icon(f, g->sky, ox + 18 * u, cy, 10.5f * u);
    const float text_x = ox + 34 * u, text_w = 32 * u;
    size = fit_size(BOX_FONT_BOLD, 15 * u, g->temp.cps, g->temp.count, text_w);
    box_font_draw(f, BOX_FONT_BOLD, size, g->temp.cps, g->temp.count, (int)text_x,
                  (int)(cy + 3 * u), WHITE);
    size = fit_size(BOX_FONT_MEDIUM, 5.2f * u, g->condition.cps, g->condition.count, text_w);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->condition.cps, g->condition.count, (int)text_x,
                  (int)(cy + 12 * u), MUTED);
    smiley(f, g->mood, ox + 81 * u, cy, 10.5f * u, face_at(a, t));
}

/* --- led ------------------------------------------------------------------- */

void box_glance_draw_led(box_frame_t *f, const box_glance_t *g, const box_glance_anim_t *a,
                         double t)
{
    box_frame_clear(f, (box_rgb_t){0, 0, 0});
    const int G = f->width < f->height ? f->width : f->height;
    const int ox = (f->width - G) / 2, oy = (f->height - G) / 2;
    int sc = (G - 2 * BOX_TEXT_MARGIN) / columns(a);
    sc = sc < 1 ? 1 : sc > 3 ? 3 : sc;
    draw_dots(f, a, t, (float)(ox + (G - columns(a) * sc) / 2), (float)(oy + G * 14 / 100),
              (float)sc, false);

    /* The temperature without the degree sign, which the bitmap font lacks;
     * a small block stands in for it. */
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
    const float r = G * 0.12f, cy = oy + G * 0.70f;
    weather_icon(f, g->sky, ox + G * 0.2f, cy, r);
    smiley(f, g->mood, ox + G * 0.8f, cy, r, face_at(a, t));
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
