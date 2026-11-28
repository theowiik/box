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

/* --- the dot clock: a little physics ---------------------------------------- */

/* Every lit dot of the time is a ball, in dot units (one unit is the gap
 * between dots, y down). Nothing is scripted: gravity pulls every ball, and
 * a ball that belongs to the time hangs from its place on a soft, slightly
 * different spring, so the digits sag and wobble a little and never sit
 * quite perfect. When the minute turns, balls the new time does not need
 * let go: they fall, bounce, knock into each other and pile up on a floor.
 * Then each place in the new time pulls the nearest loose ball up out of
 * the pile; spare balls lie there and fade, and if the pile runs short,
 * more rain in from above. */
#define ROWS 7
#define DOTS_MAX 240
#define STEP (1.0f / 120)  /* seconds per physics step */
#define CATCH_UP 0.5f      /* at most this much simulated at once */
#define GRAVITY 70.0f      /* dot units per second squared */
#define SPRING 420.0f      /* pull towards a place, per unit of distance */
#define DAMPING 0.32f      /* of critical: under 1 wobbles */
#define RADIUS 0.42f       /* a ball, as a share of the gap */
#define FLOOR 13.0f        /* where loose balls land, below the digits */
#define BOUNCE 0.45f
#define FRICTION 0.985f    /* per step, for a ball rolling on the floor */
#define PICK_UP 0.9f       /* seconds the loose balls fall before the new time pulls them */
#define FADE 1.2f          /* seconds a spare ball lies there before it is gone */
#define REST 0.02f         /* below this speed a ball is still */

typedef struct {
    float x, y, vx, vy;
    float tx, ty;  /* its place in the time, when it has one */
    float k;       /* its own spring */
    float alpha;   /* 1 lit .. 0 gone */
    bool held;     /* hanging from its place */
    bool spare;    /* loose and not wanted: fading */
} ball_t;

struct box_glance_anim {
    uint32_t shown[8];
    size_t shown_count;
    bool any;
    double now;          /* scene time simulated up to */
    double changed;      /* when the minute last turned */
    bool pending;        /* places waiting to pull balls up */
    float want[DOTS_MAX][2];
    size_t want_count;
    uint32_t seed;
    ball_t balls[DOTS_MAX];
    size_t count;
    uint64_t changes;
    bool moving;
    /* The smiley: where it looks, and when it next blinks. */
    float look, look_v;
    double blink_at;
};

static float hash01(uint32_t n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (n & 0xFFFF) / 65535.0f;
}

static float rnd(box_glance_anim_t *a)
{
    return hash01(++a->seed);
}

box_glance_anim_t *box_glance_anim_new(void)
{
    box_glance_anim_t *a = calloc(1, sizeof(*a));
    if (a) {
        a->changed = -1e9;
        a->blink_at = 2.5;
    }
    return a;
}

void box_glance_anim_free(box_glance_anim_t *a)
{
    free(a);
}

static size_t places_of(const uint32_t *cps, size_t n, float (*out)[2])
{
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *rows = box_font_glyph(cps[i]);
        for (int y = 0; y < ROWS; ++y) {
            for (int x = 0; x < BOX_GLYPH_WIDTH && count < DOTS_MAX; ++x) {
                if (rows[y] & (0x10 >> x)) {
                    out[count][0] = (float)(i * BOX_GLYPH_ADVANCE + x);
                    out[count][1] = (float)y;
                    ++count;
                }
            }
        }
    }
    return count;
}

static void hold(box_glance_anim_t *a, ball_t *b, float tx, float ty)
{
    b->tx = tx;
    b->ty = ty;
    b->k = SPRING * (0.75f + 0.5f * rnd(a)); /* no two hang quite alike */
    b->held = true;
    b->spare = false;
    b->alpha = 1;
}

static void set_time(box_glance_anim_t *a, const uint32_t *cps, size_t n, double t)
{
    static float next[DOTS_MAX][2];
    const size_t nn = places_of(cps, n, next);
    if (!a->any) { /* the first time: hanging still in place */
        a->count = 0;
        for (size_t i = 0; i < nn; ++i) {
            ball_t *b = &a->balls[a->count++];
            hold(a, b, next[i][0], next[i][1]);
            b->x = b->tx;
            b->y = b->ty + GRAVITY / b->k; /* where it rests, sagging */
            b->vx = b->vy = 0;
        }
    } else {
        /* Balls whose place is still lit keep it; the rest let go. */
        static bool kept[DOTS_MAX];
        memset(kept, 0, sizeof(kept));
        a->want_count = 0;
        for (size_t i = 0; i < a->count; ++i) {
            a->balls[i].held = false;
        }
        for (size_t j = 0; j < nn; ++j) {
            bool found = false;
            for (size_t i = 0; i < a->count && !found; ++i) {
                ball_t *b = &a->balls[i];
                if (!kept[i] && b->alpha > 0 && fabsf(b->tx - next[j][0]) < 0.01f &&
                    fabsf(b->ty - next[j][1]) < 0.01f && !b->spare) {
                    kept[i] = found = true;
                    b->held = true;
                }
            }
            if (!found) {
                a->want[a->want_count][0] = next[j][0];
                a->want[a->want_count][1] = next[j][1];
                ++a->want_count;
            }
        }
        for (size_t i = 0; i < a->count; ++i) {
            ball_t *b = &a->balls[i];
            if (!kept[i] && !b->spare) { /* let go, with a little nudge */
                b->vx += (rnd(a) - 0.5f) * 6;
                b->vy -= rnd(a) * 4;
            }
        }
        a->pending = a->want_count > 0;
        a->moving = true;
    }
    memcpy(a->shown, cps, n * sizeof(uint32_t));
    a->shown_count = n;
    a->changed = t;
    a->any = true;
    ++a->changes;
}

/* The new time pulls up the nearest loose balls; more rain in if needed. */
static void pick_up(box_glance_anim_t *a)
{
    for (size_t j = 0; j < a->want_count; ++j) {
        const float tx = a->want[j][0], ty = a->want[j][1];
        ball_t *best = NULL;
        float best_d = 1e9f;
        for (size_t i = 0; i < a->count; ++i) {
            ball_t *b = &a->balls[i];
            if (!b->held && b->alpha > 0.3f) {
                const float d = fabsf(b->x - tx) + 0.3f * fabsf(b->y - ty);
                if (d < best_d) {
                    best_d = d;
                    best = b;
                }
            }
        }
        if (!best && a->count < DOTS_MAX) {
            best = &a->balls[a->count++];
            best->x = tx + (rnd(a) - 0.5f) * 3;
            best->y = -4 - rnd(a) * 8;
            best->vx = best->vy = 0;
        }
        if (best) {
            hold(a, best, tx, ty);
        }
    }
    for (size_t i = 0; i < a->count; ++i) {
        if (!a->balls[i].held) {
            a->balls[i].spare = true;
        }
    }
    a->want_count = 0;
    a->pending = false;
}

static void step(box_glance_anim_t *a, int cols)
{
    const float dt = STEP;
    bool moving = a->pending;
    for (size_t i = 0; i < a->count; ++i) {
        ball_t *b = &a->balls[i];
        float ax = 0, ay = GRAVITY;
        if (b->held) {
            const float c = 2 * DAMPING * sqrtf(b->k);
            ax += b->k * (b->tx - b->x) - c * b->vx;
            ay += b->k * (b->ty - b->y) - c * b->vy;
        }
        b->vx += ax * dt;
        b->vy += ay * dt;
    }
    /* Balls knock into each other: push apart, and trade the speed along
     * the line between them. Held balls only meet loose ones. */
    const float d0 = 2 * RADIUS + 0.06f;
    for (size_t i = 0; i < a->count; ++i) {
        ball_t *p = &a->balls[i];
        if (p->alpha <= 0) {
            continue;
        }
        for (size_t j = i + 1; j < a->count; ++j) {
            ball_t *q = &a->balls[j];
            if (q->alpha <= 0 || (p->held && q->held)) {
                continue;
            }
            const float dx = q->x - p->x, dy = q->y - p->y;
            if (fabsf(dx) >= d0 || fabsf(dy) >= d0) {
                continue;
            }
            const float d = sqrtf(dx * dx + dy * dy);
            if (d >= d0 || d < 1e-4f) {
                continue;
            }
            const float nx = dx / d, ny = dy / d, push = (d0 - d) / 2;
            p->x -= nx * push;
            p->y -= ny * push;
            q->x += nx * push;
            q->y += ny * push;
            const float rel = (q->vx - p->vx) * nx + (q->vy - p->vy) * ny;
            if (rel < 0) {
                const float imp = -(1 + BOUNCE) * rel / 2;
                p->vx -= imp * nx;
                p->vy -= imp * ny;
                q->vx += imp * nx;
                q->vy += imp * ny;
            }
        }
    }
    for (size_t i = 0; i < a->count; ++i) {
        ball_t *b = &a->balls[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        if (!b->held) {
            if (b->y > FLOOR) {
                b->y = FLOOR;
                b->vy = b->vy > 0 ? -b->vy * BOUNCE : b->vy;
                b->vx *= FRICTION;
                if (fabsf(b->vy) < 1.5f) {
                    b->vy = 0;
                }
            }
            if (b->x < 0 || b->x > cols - 1) {
                b->x = b->x < 0 ? 0 : (float)(cols - 1);
                b->vx = -b->vx * BOUNCE;
            }
            if (b->spare) {
                b->alpha -= dt / FADE;
                if (b->alpha <= 0) {
                    *b = a->balls[--a->count]; /* gone */
                    --i;
                    continue;
                }
                moving = true;
            }
        }
        if (fabsf(b->vx) > REST || fabsf(b->vy) > REST) {
            moving = true;
        }
    }
    a->moving = moving;
}

/* --- the smiley: it watches the balls, and blinks when it likes ------------- */

#define BLINK 0.18f

typedef struct {
    float blink; /* 1 open, 0 shut */
    float look;  /* -1 left .. 1 right */
    float hop;   /* 0 resting .. 1 high */
    bool moving;
} face_t;

static void step_face(box_glance_anim_t *a, int cols, double t)
{
    /* Look at the loose balls while any move; else back to the middle. */
    float sum = 0, n = 0;
    for (size_t i = 0; i < a->count; ++i) {
        const ball_t *b = &a->balls[i];
        if (!b->held || fabsf(b->vy) > 2) {
            sum += b->x;
            ++n;
        }
    }
    const float goal = n ? fmaxf(-1, fminf(1, (sum / n / cols) * 2 - 1)) : 0;
    a->look_v += (60 * (goal - a->look) - 12 * a->look_v) * STEP;
    a->look += a->look_v * STEP;
    if (t > a->blink_at + BLINK) { /* the next blink: some time soon */
        a->blink_at = t + 2 + 5 * rnd(a);
    }
}

static face_t face_of(const box_glance_anim_t *a, double t)
{
    face_t f = {1, a->look, 0, false};
    const float b = (float)(t - a->blink_at);
    if (b >= 0 && b < BLINK) {
        f.blink = fabsf(1 - 2 * b / BLINK);
        f.moving = true;
    }
    f.moving |= fabsf(a->look_v) > 0.01f || fabsf(a->look) > 0.01f;
    return f;
}

static int columns(const box_glance_anim_t *a)
{
    return a->shown_count ? box_text_width(a->shown_count, 1) : 1;
}

uint64_t box_glance_anim_version(box_glance_anim_t *a, const uint32_t *time, size_t n, double t)
{
    if (n > 8) {
        n = 8;
    }
    if (t < a->now) { /* the scene was shown afresh: its clock restarted */
        const double back = a->now - t;
        a->now -= back;
        a->changed -= back;
        a->blink_at -= back;
    }
    if (!a->any || n != a->shown_count || memcmp(time, a->shown, n * sizeof(uint32_t))) {
        set_time(a, time, n, t);
    }
    if (t - a->now > CATCH_UP) { /* away a long time: pick up from now */
        a->now = t - CATCH_UP;
    }
    const int cols = columns(a);
    while (a->now + STEP <= t) {
        a->now += STEP;
        if (a->pending && a->now - a->changed >= PICK_UP) {
            pick_up(a);
        }
        step(a, cols);
        step_face(a, cols, a->now);
    }
    const bool moving = a->moving || face_of(a, t).moving;
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

/* Draw the balls with the clock's top-left at (x, y), `pitch` apart. Smooth
 * draws round balls; led lights the nearest block. */
static void draw_dots(box_frame_t *f, const box_glance_anim_t *a, float x, float y, float pitch,
                      bool round)
{
    for (size_t i = 0; i < a->count; ++i) {
        const ball_t *b = &a->balls[i];
        if (b->alpha <= 0.02f) {
            continue;
        }
        const box_rgb_t c = b->spare ? scale_rgb(DIM, b->alpha) : WHITE;
        if (round) {
            disc(f, x + (b->x + 0.5f) * pitch, y + (b->y + 0.5f) * pitch, pitch * RADIUS, c);
        } else {
            const int px = (int)lroundf(x + b->x * pitch), py = (int)lroundf(y + b->y * pitch);
            for (int dy = 0; dy < (int)pitch; ++dy) {
                for (int dx = 0; dx < (int)pitch; ++dx) {
                    box_frame_set_px(f, px + dx, py + dy, c);
                }
            }
        }
    }
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
    draw_dots(f, a, left, oy + 22 * u, pitch, true);

    const float cy = oy + 76 * u;
    weather_icon(f, g->sky, ox + 18 * u, cy, 10.5f * u);
    const float text_x = ox + 34 * u, text_w = 32 * u;
    size = fit_size(BOX_FONT_BOLD, 15 * u, g->temp.cps, g->temp.count, text_w);
    box_font_draw(f, BOX_FONT_BOLD, size, g->temp.cps, g->temp.count, (int)text_x,
                  (int)(cy + 3 * u), WHITE);
    size = fit_size(BOX_FONT_MEDIUM, 5.2f * u, g->condition.cps, g->condition.count, text_w);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->condition.cps, g->condition.count, (int)text_x,
                  (int)(cy + 12 * u), MUTED);
    smiley(f, g->mood, ox + 81 * u, cy, 10.5f * u, face_of(a, t));
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
    draw_dots(f, a, (float)(ox + (G - columns(a) * sc) / 2), (float)(oy + G * 14 / 100),
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
    smiley(f, g->mood, ox + G * 0.8f, cy, r, face_of(a, t));
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
