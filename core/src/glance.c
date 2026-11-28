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
static const box_rgb_t SECONDS = {165, 168, 174}; /* the clock's seconds: a little grey */
static const box_rgb_t SUN = {255, 200, 60};
static const box_rgb_t MOON = {225, 230, 245};
static const box_rgb_t CLOUD = {215, 222, 232};
static const box_rgb_t DARK_CLOUD = {120, 130, 145};
static const box_rgb_t RAIN = {90, 170, 255};
static const box_rgb_t BOLT = {255, 220, 70};
static const box_rgb_t FACE = {255, 212, 70};
static const box_rgb_t FEATURES = {70, 45, 20};

/* --- shapes: coverage from distance, so edges are antialiased ------------- */

/* Drawing stays inside this rectangle: an update redraws only what moved,
 * and must not blend twice over what it left alone. */
#define NO_CLIP ((box_rect_t){-(1 << 20), -(1 << 20), 1 << 20, 1 << 20})
static box_rect_t s_clip = {-(1 << 20), -(1 << 20), 1 << 20, 1 << 20};

static void put(box_frame_t *f, int x, int y, box_rgb_t c, float a)
{
    if (x >= s_clip.x1 && x <= s_clip.x2 && y >= s_clip.y1 && y <= s_clip.y2) {
        box_frame_blend_px(f, x, y, c, a);
    }
}


static float clamp01(float x)
{
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

static void disc(box_frame_t *f, float cx, float cy, float r, box_rgb_t c)
{
    /* Hundreds of these a frame on a small chip: clip once, write the solid
     * middle directly, and blend only the antialiased rim. */
    int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    x0 = x0 > s_clip.x1 ? x0 : s_clip.x1;
    y0 = y0 > s_clip.y1 ? y0 : s_clip.y1;
    x1 = x1 < s_clip.x2 ? x1 : s_clip.x2;
    y1 = y1 < s_clip.y2 ? y1 : s_clip.y2;
    x0 = x0 > 0 ? x0 : 0;
    y0 = y0 > 0 ? y0 : 0;
    x1 = x1 < f->width - 1 ? x1 : f->width - 1;
    y1 = y1 < f->height - 1 ? y1 : f->height - 1;
    const float in = r - 0.5f, out = r + 0.5f;
    const float in2 = in > 0 ? in * in : -1, out2 = out * out;
    for (int y = y0; y <= y1; ++y) {
        const float dy = y + 0.5f - cy;
        uint8_t *p = f->pixels + ((size_t)y * (size_t)f->width + (size_t)x0) * 3;
        for (int x = x0; x <= x1; ++x, p += 3) {
            const float dx = x + 0.5f - cx, d2 = dx * dx + dy * dy;
            if (d2 >= out2) {
                continue;
            }
            if (d2 <= in2) {
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
                continue;
            }
            const float a = r - sqrtf(d2) + 0.5f;
            p[0] = (uint8_t)(p[0] + (c.r - p[0]) * a + 0.5f);
            p[1] = (uint8_t)(p[1] + (c.g - p[1]) * a + 0.5f);
            p[2] = (uint8_t)(p[2] + (c.b - p[2]) * a + 0.5f);
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
                put(f, x, y, c, a);
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
                put(f, x, y, c, a * alpha);
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

/* --- the dot clocks: dots on springs ---------------------------------------- */

/* A time is laid out on a grid as block digits: every cell a digit covers is
 * a home for one dot (in grid units, y down). Each dot is pulled home by its own damped
 * spring, a little stiffer or softer than the next, so dots that move don't
 * move as one; once all are home they sit exactly on the grid and nothing is
 * drawn until the time changes again.
 *
 * Dots never appear or disappear. Each group of the time (the hours, each
 * colon, the minutes) owns a fixed set of dots: as many as its widest digits
 * need. When its digits need fewer, the spares rest exactly on top of another
 * dot and aren't drawn. When the time changes only the changed groups move:
 * a dot whose home is still a home keeps it, the others go to the nearest
 * free home, and spares stack on the home nearest them.
 *
 * The time is HH:MM:SS, one size; the seconds are a group like the others
 * (so a new second moves only them), drawn a little grey. */
#define COLS 84           /* grid cells across the clock */
#define ROWS 26           /* and down it: the digits are this tall */
#define DIGIT_W 10        /* a digit's width, in cells */
#define STROKE 4          /* its strokes' thickness */
#define GAP 2             /* between characters */
#define DOTS_MAX 1536
#define TIME_MAX 8        /* characters */
#define STEP (1.0f / 120) /* seconds per physics step */
#define CATCH_UP 0.1f     /* at most this much simulated at once: a slow frame slows time, not the frame after */
#define SPRING 90.0f      /* pull home, per second squared per unit away */
#define DAMPING 0.65f     /* of critical: under 1 overshoots a little */
#define KICK 11.0f        /* units per second: a dot leaving its home is nudged */
#define RADIUS 0.42f      /* a dot, as a share of the grid */
#define HOME 0.006f       /* this close and this slow, a dot is home */

typedef struct {
    float x, y, vx, vy;
    float hx, hy;   /* its home */
    float k;        /* its own spring */
    uint8_t group;
    bool spare;     /* stacked on another dot's home: drawn only on the way */
} dot_t;

typedef struct {
    int8_t x, y;
    uint8_t slot;   /* which character it belongs to */
} home_t;

/* Where a clock's grid sits on the frame: its top-left and its cell size. */
typedef struct {
    float left, top, pitch;
} place_t;

typedef struct {
    int cols, rows;
    float base, cap;          /* the digits' baseline, in cells from the top, and height */
    uint8_t grey;             /* the group drawn grey: the seconds (none: 255) */
    uint32_t shown[TIME_MAX];
    size_t shown_count;
    bool any;
    uint32_t shape[TIME_MAX]; /* the layout: '0' for every digit, else the character */
    uint8_t group_of[TIME_MAX];
    dot_t dots[DOTS_MAX];
    size_t count;
    bool moving;
    /* What the frame shows now, to redraw only what moved. */
    float drawn_x[DOTS_MAX], drawn_y[DOTS_MAX];
    bool drawn_on[DOTS_MAX];
    size_t drawn_count;
} dotclock_t;

enum { BIG, CLOCKS };

struct box_glance_anim {
    dotclock_t clock[CLOCKS];
    double now;          /* scene time simulated up to */
    uint32_t seed;
    uint64_t changes;
    /* The smiley: where it looks (across and up or down), where it wants to
     * look next and when, and when it next blinks. */
    float look, look_v, look_y, look_y_v;
    float gaze_x, gaze_y;
    double gaze_at, blink_at;
    box_frame_t still; /* what never moves, drawn once */
    float drawn_blink, drawn_look, drawn_look_y;
    place_t drawn_at; /* where the big clock was drawn: if it moves, draw it all */
    bool drawn;
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
        a->blink_at = 2.5;
        a->gaze_at = 1.5;
        a->clock[BIG].cols = COLS;
        a->clock[BIG].rows = ROWS;
    }
    return a;
}

void box_glance_anim_free(box_glance_anim_t *a)
{
    if (a) {
        box_frame_free(&a->still);
    }
    free(a);
}

static bool is_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

/* Seven segments: a top, b top right, c bottom right, d bottom, e bottom
 * left, f top left, g middle. A 1 is a bar down the middle instead (on the
 * right it leaves a hole beside it), a - is the middle segment. */
static const char *segments_of(uint32_t ch)
{
    static const char *const digits[10] = {"abcdef", "", "abged", "abgcd", "fgbc",
                                           "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};
    return ch >= '0' && ch <= '9' ? digits[ch - '0'] : ch == '-' ? "g" : "";
}

/* The homes of a time on clock `c`'s grid: block digits built on the grid
 * itself, DIGIT_W x ROWS cells with strokes STROKE cells thick and square
 * corners, GAP cells apart, the line centred. Each home knows its character. */
static size_t homes_of(dotclock_t *c, const uint32_t *cps, size_t n, home_t *out)
{
    static uint8_t lit[ROWS][COLS]; /* the character + 1 lighting each cell */
    memset(lit, 0, sizeof(lit));
    const int H = c->rows, T = STROKE, mid = (H - T) / 2;
    int line = 0;
    for (size_t i = 0; i < n; ++i) {
        line += (cps[i] == ':' ? T : DIGIT_W) + (i ? GAP : 0);
    }
    c->base = c->cap = (float)H;
    int x0 = (c->cols - line) / 2;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t mark = (uint8_t)(i + 1);
#define RECT(gx, gy, w, h)                                                                   \
        for (int y = (gy); y < (gy) + (h); ++y)                                              \
            for (int x = x0 + (gx); x < x0 + (gx) + (w); ++x)                                \
                if (x >= 0 && x < c->cols && y >= 0 && y < H) lit[y][x] = mark;
        if (cps[i] == ':') {
            RECT(0, (int)lroundf(H * 0.3f) - T / 2, T, T);
            RECT(0, (int)lroundf(H * 0.7f) - T / 2, T, T);
            x0 += T + GAP;
            continue;
        }
        if (cps[i] == '1') {
            RECT((DIGIT_W - T) / 2, 0, T, H);
        }
        for (const char *s = segments_of(cps[i]); *s; ++s) {
            switch (*s) {
            case 'a': RECT(0, 0, DIGIT_W, T); break;
            case 'g': RECT(0, mid, DIGIT_W, T); break;
            case 'd': RECT(0, H - T, DIGIT_W, T); break;
            case 'f': RECT(0, 0, T, mid + T); break;
            case 'b': RECT(DIGIT_W - T, 0, T, mid + T); break;
            case 'e': RECT(0, mid, T, H - mid); break;
            case 'c': RECT(DIGIT_W - T, mid, T, H - mid); break;
            default: break;
            }
        }
#undef RECT
        x0 += DIGIT_W + GAP;
    }
    size_t count = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < c->cols && count < DOTS_MAX; ++x) {
            if (lit[y][x]) {
                out[count++] = (home_t){(int8_t)x, (int8_t)y, (uint8_t)(lit[y][x] - 1)};
            }
        }
    }
    return count;
}

/* A new layout (the first time, or a time of another shape): sort the
 * characters into groups and give each group its dots, enough for its
 * widest digits. */
static void lay_out(box_glance_anim_t *a, dotclock_t *c, const uint32_t *cps, size_t n)
{
    static home_t homes[DOTS_MAX];
    size_t most[TIME_MAX] = {0};
    uint32_t probe[TIME_MAX];
    for (uint32_t d = '0'; d <= '9'; ++d) {
        for (size_t i = 0; i < n; ++i) {
            probe[i] = is_digit(cps[i]) ? d : cps[i];
        }
        size_t per[TIME_MAX] = {0};
        const size_t nh = homes_of(c, probe, n, homes);
        for (size_t j = 0; j < nh; ++j) {
            ++per[homes[j].slot];
        }
        for (size_t i = 0; i < n; ++i) {
            most[i] = per[i] > most[i] ? per[i] : most[i];
        }
    }
    uint8_t group = 0;
    for (size_t i = 0; i < n; ++i) {
        c->shape[i] = is_digit(cps[i]) ? '0' : cps[i];
        if (i > 0 && !(is_digit(cps[i]) && is_digit(cps[i - 1]))) {
            ++group;
        }
        c->group_of[i] = group;
    }
    c->grey = n == 8 ? c->group_of[7] : 255; /* HH:MM:SS */
    c->count = 0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < most[i] && c->count < DOTS_MAX; ++k) {
            dot_t *d = &c->dots[c->count++];
            *d = (dot_t){0};
            d->group = c->group_of[i];
            d->k = SPRING * (0.75f + 0.5f * rnd(a)); /* no two quite alike */
            d->hx = d->hy = -1;
        }
    }
}

static void send_home(box_glance_anim_t *a, dot_t *d, float hx, float hy, bool spare, bool kick)
{
    if (kick && (d->hx != hx || d->hy != hy)) { /* nudged as it leaves */
        d->vx += (rnd(a) - 0.5f) * KICK;
        d->vy += (rnd(a) - 0.5f) * KICK - KICK * 0.3f;
    }
    d->hx = hx;
    d->hy = hy;
    d->spare = spare;
}

/* Match a group's dots to its new homes. */
static void settle_group(box_glance_anim_t *a, dotclock_t *c, uint8_t group, const home_t *homes,
                         size_t nh, bool kick)
{
    static bool taken[DOTS_MAX];
    static uint16_t loose[DOTS_MAX], open[DOTS_MAX];
    size_t nl = 0, no = 0;
    memset(taken, 0, sizeof(taken));
    /* Dots on a home that is still a home keep it: dots that show first,
     * then spares, so a spare never takes a home from the dot under it. */
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < c->count; ++i) {
            dot_t *d = &c->dots[i];
            if (d->group != group || d->spare != (pass == 1)) {
                continue;
            }
            size_t j = 0;
            while (j < nh && (taken[j] || homes[j].x != d->hx || homes[j].y != d->hy)) {
                ++j;
            }
            if (j < nh) {
                taken[j] = true;
                d->spare = false;
            } else {
                loose[nl++] = (uint16_t)i;
            }
        }
    }
    for (size_t j = 0; j < nh; ++j) {
        if (!taken[j]) {
            open[no++] = (uint16_t)j;
        }
    }
    /* Each free home, in a shuffled order, takes the nearest loose dot. */
    for (size_t j = no; j > 1; --j) {
        const size_t r = (size_t)(rnd(a) * j) % j;
        const uint16_t t = open[j - 1];
        open[j - 1] = open[r];
        open[r] = t;
    }
    for (size_t j = 0; j < no && nl; ++j) {
        const home_t *h = &homes[open[j]];
        size_t best = 0;
        float best_d = 1e9f;
        for (size_t l = 0; l < nl; ++l) {
            const dot_t *d = &c->dots[loose[l]];
            const float e = (d->x - h->x) * (d->x - h->x) + (d->y - h->y) * (d->y - h->y);
            if (e < best_d) {
                best_d = e;
                best = l;
            }
        }
        send_home(a, &c->dots[loose[best]], h->x, h->y, false, kick);
        loose[best] = loose[--nl];
    }
    /* Spares rest on the home nearest them. */
    for (size_t l = 0; l < nl && nh; ++l) {
        dot_t *d = &c->dots[loose[l]];
        size_t best = 0;
        float best_d = 1e9f;
        for (size_t j = 0; j < nh; ++j) {
            const float e = (d->x - homes[j].x) * (d->x - homes[j].x) +
                            (d->y - homes[j].y) * (d->y - homes[j].y);
            if (e < best_d) {
                best_d = e;
                best = j;
            }
        }
        send_home(a, d, homes[best].x, homes[best].y, true, kick);
    }
}

static void set_time(box_glance_anim_t *a, dotclock_t *c, const uint32_t *cps, size_t n)
{
    static home_t homes[DOTS_MAX], mine[DOTS_MAX];
    bool fresh = !c->any || n != c->shown_count;
    for (size_t i = 0; i < n && !fresh; ++i) {
        fresh = c->shape[i] != (is_digit(cps[i]) ? '0' : cps[i]);
    }
    if (fresh) {
        lay_out(a, c, cps, n);
    }
    const size_t nh = homes_of(c, cps, n, homes);
    for (uint8_t g = 0; n && g <= c->group_of[n - 1]; ++g) {
        bool same = !fresh;
        for (size_t i = 0; i < n && same; ++i) {
            same = c->group_of[i] != g || c->shown[i] == cps[i];
        }
        if (same) {
            continue; /* this group shows what it showed: it stays still */
        }
        size_t nm = 0;
        for (size_t j = 0; j < nh; ++j) {
            if (c->group_of[homes[j].slot] == g) {
                mine[nm++] = homes[j];
            }
        }
        settle_group(a, c, g, mine, nm, !fresh);
        c->moving = true;
    }
    if (fresh) { /* the first time: everything already home */
        for (size_t i = 0; i < c->count; ++i) {
            c->dots[i].x = c->dots[i].hx;
            c->dots[i].y = c->dots[i].hy;
        }
        c->moving = false;
    }
    if (!n) {
        c->count = 0;
    }
    memcpy(c->shown, cps, n * sizeof(uint32_t));
    c->shown_count = n;
    c->any = true;
    ++a->changes;
}

static void step(dotclock_t *c)
{
    if (!c->moving) {
        return;
    }
    const float dt = STEP;
    bool busy = false;
    for (size_t i = 0; i < c->count; ++i) {
        dot_t *d = &c->dots[i];
        const float k = d->k, damp = 2 * DAMPING * sqrtf(k); /* damped spring, unit mass */
        d->vx += (k * (d->hx - d->x) - damp * d->vx) * dt;
        d->vy += (k * (d->hy - d->y) - damp * d->vy) * dt;
        d->x += d->vx * dt;
        d->y += d->vy * dt;
        busy |= fabsf(d->x - d->hx) + fabsf(d->y - d->hy) > HOME ||
                fabsf(d->vx) + fabsf(d->vy) > 0.1f;
    }
    if (!busy) { /* all home: exactly on the grid, and still */
        for (size_t i = 0; i < c->count; ++i) {
            dot_t *d = &c->dots[i];
            d->x = d->hx;
            d->y = d->hy;
            d->vx = d->vy = 0;
        }
        c->moving = false;
    }
}

/* A dot is drawn unless it is a spare resting under another. */
static bool shows(const dot_t *d)
{
    return !d->spare || fabsf(d->x - d->hx) + fabsf(d->y - d->hy) > 0.3f;
}

static box_rgb_t colour_of(const dotclock_t *c, const dot_t *d)
{
    return d->group == c->grey ? SECONDS : WHITE;
}

/* --- the smiley: it looks around, watches the dots, blinks when it likes --- */

#define BLINK 0.18f

typedef struct {
    float blink; /* 1 open, 0 shut */
    float look;  /* -1 left .. 1 right */
    float hop;   /* 0 resting .. 1 high */
    bool moving;
    float look_y; /* -1 up .. 1 down */
} face_t;

/* A damped spring towards `goal`, coming exactly to rest on it. */
static void ease(float *v, float *vel, float goal)
{
    *vel += (120 * (goal - *v) - 17 * *vel) * STEP;
    *v += *vel * STEP;
    if (fabsf(goal - *v) < 0.002f && fabsf(*vel) < 0.01f) {
        *v = goal;
        *vel = 0;
    }
}

static void step_face(box_glance_anim_t *a, double t)
{
    /* Look up at the big clock's dots on their way while any fly. (The
     * seconds fly every second: it would never look anywhere else.) Else it
     * looks around: every few seconds somewhere new, now and then straight
     * ahead. */
    const dotclock_t *c = &a->clock[BIG];
    float sum = 0, n = 0;
    for (size_t i = 0; i < c->count && c->moving; ++i) {
        const dot_t *d = &c->dots[i];
        if (d->group != c->grey && fabsf(d->x - d->hx) + fabsf(d->y - d->hy) > 0.5f) {
            sum += d->x;
            ++n;
        }
    }
    if (!n && t >= a->gaze_at) {
        const bool ahead = rnd(a) < 0.25f;
        a->gaze_x = ahead ? 0 : rnd(a) * 2 - 1;
        a->gaze_y = ahead ? 0 : rnd(a) * 1.4f - 0.9f; /* more often up than down */
        a->gaze_at = t + 1.2 + 3.5 * rnd(a);
    }
    const float goal = n ? fmaxf(-1, fminf(1, (sum / n / COLS) * 2 - 1)) : a->gaze_x;
    ease(&a->look, &a->look_v, goal);
    ease(&a->look_y, &a->look_y_v, n ? -0.8f : a->gaze_y);
    if (t > a->blink_at + BLINK) { /* the next blink: some time soon */
        a->blink_at = t + 2 + 5 * rnd(a);
    }
}

static face_t face_of(const box_glance_anim_t *a, double t)
{
    face_t f = {1, a->look, 0, false, a->look_y};
    const float b = (float)(t - a->blink_at);
    if (b >= 0 && b < BLINK) {
        f.blink = fabsf(1 - 2 * b / BLINK);
        f.moving = true;
    }
    f.moving |= a->look_v != 0 || a->look_y_v != 0;
    return f;
}

uint64_t box_glance_anim_version(box_glance_anim_t *a, const uint32_t *time, size_t n,
                                 int second, double t)
{
    if (n > TIME_MAX) {
        n = TIME_MAX;
    }
    if (t < a->now) { /* the scene was shown afresh: its clock restarted */
        const double back = a->now - t;
        a->now -= back;
        a->blink_at -= back;
        a->gaze_at -= back;
    }
    /* HH:MM, and :SS after it when the seconds are known. */
    uint32_t full[TIME_MAX];
    memcpy(full, time, n * sizeof(uint32_t));
    if (n == 5 && second >= 0 && second < 60) {
        full[n++] = ':';
        full[n++] = (uint32_t)('0' + second / 10);
        full[n++] = (uint32_t)('0' + second % 10);
    }
    dotclock_t *big = &a->clock[BIG];
    if (!big->any || n != big->shown_count || memcmp(full, big->shown, n * sizeof(uint32_t))) {
        set_time(a, big, full, n);
    }
    if (t - a->now > CATCH_UP) { /* away a long time: pick up from now */
        a->now = t - CATCH_UP;
    }
    while (a->now + STEP <= t) {
        a->now += STEP;
        step(big);
        step_face(a, a->now);
    }
    const bool moving = big->moving || face_of(a, t).moving;
    /* A frame number while anything moves; otherwise still. */
    return (a->changes << 32) + (moving ? (uint64_t)(t * 30) + 1 : 0);
}

static void smiley(box_frame_t *f, box_glance_mood_t mood, float cx, float cy, float r, face_t m)
{
    cy -= m.hop * r * 0.35f;
    disc(f, cx, cy, r, FACE);
    const float pi = (float)M_PI, w = r * 0.13f;
    const float ex = r * 0.33f, ey = cy - r * 0.2f + m.look_y * r * 0.12f, er = r * 0.12f;
    const float shift = m.look * r * 0.14f, my = m.look_y * r * 0.05f;
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
        arc(f, mx, cy + my + r * 0.02f, r * 0.5f, pi * 0.15f, pi * 0.85f, w, FEATURES);
        break;
    case BOX_MOOD_GOOD:
        arc(f, mx, cy + my - r * 0.1f, r * 0.5f, pi * 0.3f, pi * 0.7f, w, FEATURES);
        break;
    case BOX_MOOD_MEH:
        stroke(f, mx - r * 0.3f, cy + my + r * 0.38f, mx + r * 0.3f, cy + my + r * 0.38f, w, FEATURES);
        break;
    case BOX_MOOD_SAD:
    default:
        arc(f, mx, cy + my + r * 0.75f, r * 0.42f, pi * 1.25f, pi * 1.75f, w, FEATURES);
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

/* Draw a clock's dots. Smooth draws round dots; led lights the nearest block. */
static void draw_dots(box_frame_t *f, const dotclock_t *c, place_t p, bool round)
{
    for (size_t i = 0; i < c->count; ++i) {
        const dot_t *b = &c->dots[i];
        if (!shows(b)) {
            continue;
        }
        if (round) {
            disc(f, p.left + (b->x + 0.5f) * p.pitch, p.top + (b->y + 0.5f) * p.pitch,
                 p.pitch * RADIUS, colour_of(c, b));
        } else {
            const int px = (int)lroundf(p.left + b->x * p.pitch);
            const int py = (int)lroundf(p.top + b->y * p.pitch);
            for (int dy = 0; dy < (int)p.pitch; ++dy) {
                for (int dx = 0; dx < (int)p.pitch; ++dx) {
                    box_frame_set_px(f, px + dx, py + dy, colour_of(c, b));
                }
            }
        }
    }
}

/* --- smooth ---------------------------------------------------------------- */

/* Text and the weather, which never move: drawn once per scene and size. */
static void draw_still(box_frame_t *f, const box_glance_t *g)
{
    box_frame_clear(f, (box_rgb_t){0, 0, 0}); /* an OLED: black is off */
    const float S = (float)(f->width < f->height ? f->width : f->height), u = S / 100;
    const float ox = (f->width - S) / 2, oy = (f->height - S) / 2;

    const float left = ox + 8 * u, width = 84 * u;
    float size = fit_size(BOX_FONT_MEDIUM, 6.5f * u, g->date.cps, g->date.count, width);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->date.cps, g->date.count, (int)left,
                  (int)(oy + 14 * u), scale_rgb(WHITE, 0.6f));
    const float cy = oy + 76 * u;
    weather_icon(f, g->sky, ox + 18 * u, cy, 10.5f * u);
    const float text_x = ox + 34 * u, text_w = 32 * u;
    size = fit_size(BOX_FONT_BOLD, 15 * u, g->temp.cps, g->temp.count, text_w);
    box_font_draw(f, BOX_FONT_BOLD, size, g->temp.cps, g->temp.count, (int)text_x,
                  (int)(cy + 3 * u), WHITE);
    size = fit_size(BOX_FONT_MEDIUM, 5.2f * u, g->condition.cps, g->condition.count, text_w);
    box_font_draw(f, BOX_FONT_MEDIUM, size, g->condition.cps, g->condition.count, (int)text_x,
                  (int)(cy + 12 * u), MUTED);
}

/* Where the moving parts go on a frame of this size: the time across the
 * top (HH:MM, then the seconds on the same baseline), the smiley bottom
 * right. */
typedef struct {
    place_t clock[CLOCKS];
    float face_x, face_y, face_r;
} geometry_t;

static geometry_t geometry(const box_frame_t *f, const box_glance_anim_t *a)
{
    const float S = (float)(f->width < f->height ? f->width : f->height), u = S / 100;
    const float ox = (f->width - S) / 2, oy = (f->height - S) / 2;
    const dotclock_t *big = &a->clock[BIG];
    geometry_t g;
    /* One line 84 wide, the digits centred where they always stood. */
    const float p = 84 * u / COLS;
    g.clock[BIG] = (place_t){ox + 8 * u, oy + 39.5f * u + big->cap * p / 2 - big->base * p, p};
    g.face_x = ox + 81 * u;
    g.face_y = oy + 76 * u;
    g.face_r = 10.5f * u;
    return g;
}

static box_rect_t ball_rect(place_t p, float x, float y)
{
    const float cx = p.left + (x + 0.5f) * p.pitch, cy = p.top + (y + 0.5f) * p.pitch;
    const float r = p.pitch * RADIUS + 1.5f;
    return (box_rect_t){(int)floorf(cx - r), (int)floorf(cy - r), (int)ceilf(cx + r),
                        (int)ceilf(cy + r)};
}

static box_rect_t face_rect(const geometry_t *g)
{
    const float r = g->face_r;
    return (box_rect_t){(int)(g->face_x - r - 2), (int)(g->face_y - r * 1.4f - 2),
                        (int)(g->face_x + r + 2), (int)(g->face_y + r + 2)};
}

static void grow(box_rect_t *u, box_rect_t r)
{
    u->x1 = r.x1 < u->x1 ? r.x1 : u->x1;
    u->y1 = r.y1 < u->y1 ? r.y1 : u->y1;
    u->x2 = r.x2 > u->x2 ? r.x2 : u->x2;
    u->y2 = r.y2 > u->y2 ? r.y2 : u->y2;
}

static bool inside(box_rect_t a, box_rect_t b)
{
    return a.x1 >= b.x1 && a.y1 >= b.y1 && a.x2 <= b.x2 && a.y2 <= b.y2;
}

static bool meets(box_rect_t a, box_rect_t b)
{
    return a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2;
}

/* Draw the dots and the face that touch the clip, and remember them. */
static void draw_moving(box_frame_t *f, const box_glance_t *g, box_glance_anim_t *a, double t,
                        const geometry_t *geo)
{
    for (int k = 0; k < CLOCKS; ++k) {
        dotclock_t *c = &a->clock[k];
        const place_t p = geo->clock[k];
        for (size_t i = 0; i < c->count; ++i) {
            const dot_t *b = &c->dots[i];
            if (i >= c->drawn_count || inside(ball_rect(p, b->x, b->y), s_clip)) {
                /* Only a dot drawn here shows where it is now. */
                c->drawn_x[i] = b->x;
                c->drawn_y[i] = b->y;
                c->drawn_on[i] = shows(b);
            }
            if (shows(b) && meets(ball_rect(p, b->x, b->y), s_clip)) {
                disc(f, p.left + (b->x + 0.5f) * p.pitch, p.top + (b->y + 0.5f) * p.pitch,
                     p.pitch * RADIUS, colour_of(c, b));
            }
        }
        c->drawn_count = c->count;
    }
    const face_t face = face_of(a, t);
    if (meets(face_rect(geo), s_clip)) {
        smiley(f, g->mood, geo->face_x, geo->face_y, geo->face_r, face);
        if (inside(face_rect(geo), s_clip)) {
            a->drawn_blink = face.blink;
            a->drawn_look = face.look;
            a->drawn_look_y = face.look_y;
        }
    }
    a->drawn = true;
}

void box_glance_draw_smooth(box_frame_t *f, const box_glance_t *g, box_glance_anim_t *a, double t)
{
    if (a->still.width != f->width || a->still.height != f->height) {
        box_frame_free(&a->still);
        if (box_frame_init(&a->still, f->width, f->height)) {
            draw_still(&a->still, g);
        }
    }
    if (a->still.pixels) {
        memcpy(f->pixels, a->still.pixels, box_frame_bytes(f));
    } else {
        draw_still(f, g);
    }
    const geometry_t geo = geometry(f, a);
    s_clip = NO_CLIP;
    draw_moving(f, g, a, t, &geo);
    a->drawn_at = geo.clock[BIG];
    box_frame_changed_all(f);
}

/* The rectangles a dot covered (where it was drawn) and covers (where it is). */
static int dot_rects(const dotclock_t *c, place_t p, size_t i, box_rect_t r[2])
{
    int k = 0;
    if (i < c->drawn_count) {
        r[k++] = ball_rect(p, c->drawn_x[i], c->drawn_y[i]);
    }
    if (i < c->count) {
        r[k++] = ball_rect(p, c->dots[i].x, c->dots[i].y);
    }
    return k;
}

void box_glance_draw_smooth_again(box_frame_t *f, const box_glance_t *g, box_glance_anim_t *a,
                                  double t)
{
    if (!a->drawn || !a->still.pixels || a->still.width != f->width ||
        a->still.height != f->height) {
        box_glance_draw_smooth(f, g, a, t);
        return;
    }
    /* What moved: dots where they were and where they are, and the face. */
    const geometry_t geo = geometry(f, a);
    if (memcmp(&geo.clock[BIG], &a->drawn_at, sizeof(place_t))) {
        box_glance_draw_smooth(f, g, a, t); /* the line was laid out anew */
        return;
    }
    box_rect_t u = {f->width, f->height, -1, -1};
    for (int k = 0; k < CLOCKS; ++k) {
        const dotclock_t *c = &a->clock[k];
        const place_t p = geo.clock[k];
        const size_t n = c->count > c->drawn_count ? c->count : c->drawn_count;
        for (size_t i = 0; i < n; ++i) {
            const dot_t *b = i < c->count ? &c->dots[i] : NULL;
            if (b && i < c->drawn_count && fabsf(b->x - c->drawn_x[i]) * p.pitch < 0.02f &&
                fabsf(b->y - c->drawn_y[i]) * p.pitch < 0.02f && shows(b) == c->drawn_on[i]) {
                continue;
            }
            box_rect_t r[2];
            for (int j = dot_rects(c, p, i, r); j-- > 0;) {
                grow(&u, r[j]);
            }
        }
    }
    const face_t face = face_of(a, t);
    if (fabsf(face.blink - a->drawn_blink) > 0.001f || fabsf(face.look - a->drawn_look) > 0.001f ||
        fabsf(face.look_y - a->drawn_look_y) > 0.001f) {
        grow(&u, face_rect(&geo));
    }
    /* Whatever the area touches is redrawn whole: grow it until nothing
     * sticks out half drawn. */
    for (bool grew = u.x2 >= u.x1; grew;) {
        grew = false;
        for (int k = 0; k < CLOCKS; ++k) {
            const dotclock_t *c = &a->clock[k];
            const size_t n = c->count > c->drawn_count ? c->count : c->drawn_count;
            for (size_t i = 0; i < n; ++i) {
                box_rect_t r[2];
                for (int j = dot_rects(c, geo.clock[k], i, r); j-- > 0;) {
                    if (meets(r[j], u) && !inside(r[j], u)) {
                        grow(&u, r[j]);
                        grew = true;
                    }
                }
            }
        }
        if (meets(face_rect(&geo), u) && !inside(face_rect(&geo), u)) {
            grow(&u, face_rect(&geo));
            grew = true;
        }
    }
    u.x1 = u.x1 < 0 ? 0 : u.x1;
    u.y1 = u.y1 < 0 ? 0 : u.y1;
    u.x2 = u.x2 >= f->width ? f->width - 1 : u.x2;
    u.y2 = u.y2 >= f->height ? f->height - 1 : u.y2;
    f->changed = u;
    if (u.x2 < u.x1 || u.y2 < u.y1) {
        return; /* nothing moved enough to see */
    }
    /* Put back what never moves there, then draw what touches it. */
    const size_t row = (size_t)(u.x2 - u.x1 + 1) * 3;
    for (int y = u.y1; y <= u.y2; ++y) {
        const size_t at = ((size_t)y * f->width + u.x1) * 3;
        memcpy(f->pixels + at, a->still.pixels + at, row);
    }
    s_clip = u;
    draw_moving(f, g, a, t, &geo);
    s_clip = NO_CLIP;
}

/* --- led ------------------------------------------------------------------- */

void box_glance_draw_led(box_frame_t *f, const box_glance_t *g, const box_glance_anim_t *a,
                         double t)
{
    box_frame_clear(f, (box_rgb_t){0, 0, 0});
    const int G = f->width < f->height ? f->width : f->height;
    const int ox = (f->width - G) / 2, oy = (f->height - G) / 2;
    int sc = (G - 2 * BOX_TEXT_MARGIN) / COLS;
    sc = sc < 1 ? 1 : sc > 3 ? 3 : sc;
    draw_dots(f, &a->clock[BIG],
              (place_t){(float)(ox + (G - COLS * sc) / 2), (float)(oy + G * 14 / 100), (float)sc}, false);

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
