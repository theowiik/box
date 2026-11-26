#include "box/scene.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "box/text.h"

/* In the led style a title's lines are drawn at this share of the colour. */
#define LED_TITLE_DIM 0.55f
#define CLOCK_CHARS 5 /* "HH:MM" */

typedef struct {
    char *utf8;
    uint32_t *cps;
    size_t count;
} text_t;

struct box_scene {
    int refs;
    box_content_t content;
    box_style_t style;
    box_rgb_t color;
    /* Text and clock. `both` is title + '\n' + text, for the bitmap font. */
    text_t title, text, both;
    box_align_t align;
    box_card_layout_t *card;
    int card_w, card_h;
    box_text_layout_t *bitmap;
    int bitmap_w, bitmap_h;
    /* Clock. */
    box_clock_fn clock;
    void *clock_ctx;
    /* Image: flat RGB frames. */
    uint8_t *data;
    size_t size, stride, count;
    int width, height;
    float fps;
    bool loop;
    box_scaling_t scaling;
};

static atomic_size_t s_images_in_use;
static size_t s_images_budget = 12u * 1024u * 1024u;

void box_images_set_budget(size_t bytes)
{
    s_images_budget = bytes;
}

bool box_images_budget_allows(size_t bytes)
{
    const size_t used = atomic_load(&s_images_in_use);
    return bytes <= s_images_budget && used <= s_images_budget - bytes;
}

size_t box_images_in_use(void)
{
    return atomic_load(&s_images_in_use);
}

static const char *const STYLE_NAMES[] = {
    [BOX_STYLE_DEFAULT] = "default",
    [BOX_STYLE_SMOOTH] = "smooth",
    [BOX_STYLE_LED] = "led",
};

const char *box_style_name(box_style_t style)
{
    return STYLE_NAMES[style];
}

bool box_style_parse(const char *name, box_style_t *out)
{
    for (int i = BOX_STYLE_SMOOTH; i <= BOX_STYLE_LED; ++i) {
        if (strcmp(name, STYLE_NAMES[i]) == 0) {
            *out = (box_style_t)i;
            return true;
        }
    }
    return false;
}

/* --- construction ---------------------------------------------------------- */

static box_scene_t *alloc_scene(box_content_t content)
{
    box_scene_t *scene = calloc(1, sizeof(*scene));
    if (scene) {
        scene->refs = 1;
        scene->content = content;
    }
    return scene;
}

static bool text_set(text_t *t, const char *utf8, size_t len)
{
    char *copy = malloc(len + 1);
    const size_t count = box_utf8_count(utf8, len);
    uint32_t *cps = malloc((count ? count : 1) * sizeof(uint32_t));
    if (!copy || !cps) {
        free(copy);
        free(cps);
        return false;
    }
    memcpy(copy, utf8, len);
    copy[len] = '\0';
    free(t->utf8);
    free(t->cps);
    t->utf8 = copy;
    t->cps = cps;
    t->count = box_utf8_decode(utf8, len, cps, count);
    return true;
}

static void text_free(text_t *t)
{
    free(t->utf8);
    free(t->cps);
}

box_scene_t *box_scene_new_clear(void)
{
    return alloc_scene(BOX_CONTENT_CLEAR);
}

box_scene_t *box_scene_new_color(box_rgb_t color)
{
    box_scene_t *scene = alloc_scene(BOX_CONTENT_COLOR);
    if (scene) {
        scene->color = color;
    }
    return scene;
}

static bool set_both(box_scene_t *scene)
{
    const size_t tl = strlen(scene->title.utf8), bl = strlen(scene->text.utf8);
    char *joined = malloc(tl + bl + 2);
    if (!joined) {
        return false;
    }
    memcpy(joined, scene->title.utf8, tl);
    size_t n = tl;
    if (tl) {
        joined[n++] = '\n';
    }
    memcpy(joined + n, scene->text.utf8, bl);
    n += bl;
    const bool ok = text_set(&scene->both, joined, n);
    free(joined);
    return ok;
}

box_scene_t *box_scene_new_text(const char *title, size_t title_len, const char *text,
                                size_t text_len, box_rgb_t color, box_align_t align)
{
    box_scene_t *scene = alloc_scene(BOX_CONTENT_TEXT);
    if (!scene) {
        return NULL;
    }
    scene->color = color;
    scene->align = align;
    if (!text_set(&scene->title, title ? title : "", title ? title_len : 0) ||
        !text_set(&scene->text, text, text_len) || !set_both(scene)) {
        box_scene_release(scene);
        return NULL;
    }
    return scene;
}

box_scene_t *box_scene_new_image(uint8_t *data, size_t size, int width, int height, float fps,
                                 bool loop, box_scaling_t scaling)
{
    const size_t stride = width > 0 && height > 0 ? (size_t)width * (size_t)height * 3 : 0;
    if (!data || stride == 0 || size == 0 || size % stride) {
        free(data);
        return NULL;
    }
    box_scene_t *scene = alloc_scene(BOX_CONTENT_IMAGE);
    if (!scene) {
        free(data);
        return NULL;
    }
    scene->data = data;
    scene->size = size;
    scene->stride = stride;
    scene->count = size / stride;
    scene->width = width;
    scene->height = height;
    scene->fps = fps > 0.1f ? fps : 0.1f;
    scene->loop = loop;
    scene->scaling = scaling;
    atomic_fetch_add(&s_images_in_use, size);
    return scene;
}

box_scene_t *box_scene_new_clock(box_clock_fn now, void *ctx, box_rgb_t color)
{
    box_scene_t *scene = alloc_scene(BOX_CONTENT_CLOCK);
    if (!scene) {
        return NULL;
    }
    scene->clock = now;
    scene->clock_ctx = ctx;
    scene->color = color;
    scene->align = BOX_ALIGN_CENTER;
    if (!now || !text_set(&scene->title, "", 0) || !text_set(&scene->text, "", 0) ||
        !text_set(&scene->both, "", 0)) {
        box_scene_release(scene);
        return NULL;
    }
    return scene;
}

void box_scene_set_style(box_scene_t *scene, box_style_t style)
{
    scene->style = style;
}

box_style_t box_scene_style(const box_scene_t *scene)
{
    return scene->style;
}

box_scene_t *box_scene_retain(box_scene_t *scene)
{
    if (scene) {
        ++scene->refs;
    }
    return scene;
}

void box_scene_release(box_scene_t *scene)
{
    if (!scene || --scene->refs > 0) {
        return;
    }
    if (scene->data) {
        atomic_fetch_sub(&s_images_in_use, scene->size);
    }
    text_free(&scene->title);
    text_free(&scene->text);
    text_free(&scene->both);
    free(scene->card);
    free(scene->bitmap);
    free(scene->data);
    free(scene);
}

/* --- reading ----------------------------------------------------------------- */

box_content_t box_scene_content(const box_scene_t *scene)
{
    return scene->content;
}

const char *box_scene_content_name(const box_scene_t *scene)
{
    switch (scene->content) {
    case BOX_CONTENT_COLOR:
        return "color";
    case BOX_CONTENT_TEXT:
        return "text";
    case BOX_CONTENT_IMAGE:
        return "image";
    case BOX_CONTENT_CLOCK:
        return "clock";
    case BOX_CONTENT_CLEAR:
    default:
        return "clear";
    }
}

const char *box_scene_title(const box_scene_t *scene)
{
    return scene->title.utf8 ? scene->title.utf8 : "";
}

const char *box_scene_text(const box_scene_t *scene)
{
    return scene->text.utf8 ? scene->text.utf8 : "";
}

box_rgb_t box_scene_color(const box_scene_t *scene)
{
    return scene->color;
}

static size_t image_index(const box_scene_t *scene, double t)
{
    size_t index = t > 0 ? (size_t)(t * scene->fps) : 0;
    if (index >= scene->count) {
        index = scene->loop ? index % scene->count : scene->count - 1;
    }
    return index;
}

/* Refresh a clock's text; returns minutes since midnight, or -1 if unknown. */
static int update_clock(box_scene_t *scene)
{
    int hour, minute;
    char now[CLOCK_CHARS + 1] = "";
    int stamp = -1;
    if (scene->clock(scene->clock_ctx, &hour, &minute) && hour >= 0 && hour < 24 &&
        minute >= 0 && minute < 60) {
        snprintf(now, sizeof(now), "%02d:%02d", hour, minute);
        stamp = hour * 60 + minute;
    }
    if (strcmp(now, scene->text.utf8) != 0) {
        text_set(&scene->text, now, strlen(now));
        text_set(&scene->both, now, strlen(now));
        scene->card_w = scene->bitmap_w = 0; /* lay the new time out */
    }
    return stamp;
}

uint64_t box_scene_version(box_scene_t *scene, double t)
{
    switch (scene->content) {
    case BOX_CONTENT_IMAGE:
        return image_index(scene, t);
    case BOX_CONTENT_CLOCK:
        return (uint64_t)(update_clock(scene) + 1);
    default:
        return 0;
    }
}

/* --- drawing ------------------------------------------------------------------ */

static const box_rgb_t BLACK = {0, 0, 0};

static void draw_text_smooth(box_scene_t *scene, box_frame_t *frame)
{
    if (!scene->card || scene->card_w != frame->width || scene->card_h != frame->height) {
        if (!scene->card && !(scene->card = malloc(sizeof(*scene->card)))) {
            return;
        }
        box_card_layout(scene->title.cps, scene->title.count, scene->text.cps, scene->text.count,
                        frame->width, frame->height, scene->card);
        scene->card_w = frame->width;
        scene->card_h = frame->height;
    }
    box_card_draw(frame, scene->card, scene->title.cps, scene->text.cps, scene->align,
                  scene->color);
}

static void draw_text_led(box_scene_t *scene, box_frame_t *frame)
{
    if (!scene->bitmap || scene->bitmap_w != frame->width || scene->bitmap_h != frame->height) {
        if (!scene->bitmap && !(scene->bitmap = malloc(sizeof(*scene->bitmap)))) {
            return;
        }
        box_text_layout(scene->both.cps, scene->both.count, frame->width, frame->height,
                        scene->bitmap);
        scene->bitmap_w = frame->width;
        scene->bitmap_h = frame->height;
    }
    const box_text_layout_t *l = scene->bitmap;
    if (!l->line_count) {
        return;
    }
    const box_rgb_t dim = {
        (uint8_t)(scene->color.r * LED_TITLE_DIM),
        (uint8_t)(scene->color.g * LED_TITLE_DIM),
        (uint8_t)(scene->color.b * LED_TITLE_DIM),
    };
    const size_t title_end = scene->title.count; /* code points before the '\n' */
    const int sc = l->scale;
    const int block = (int)l->line_count * BOX_LINE_ADVANCE * sc - sc;
    int y = (frame->height - block) / 2;
    static const uint32_t ellipsis[] = {'.', '.', '.'};
    for (size_t i = 0; i < l->line_count; ++i, y += BOX_LINE_ADVANCE * sc) {
        const box_text_line_t *line = &l->lines[i];
        const box_rgb_t c = title_end && line->start < title_end ? dim : scene->color;
        const bool cut = l->truncated && i + 1 == l->line_count;
        const size_t glyphs = line->count + (cut ? 3 : 0);
        const int x = scene->align == BOX_ALIGN_LEFT
                          ? BOX_TEXT_MARGIN
                          : (frame->width - box_text_width(glyphs, sc)) / 2;
        box_frame_draw_text(frame, scene->both.cps + line->start, line->count, x, y, sc, c);
        if (cut) {
            box_frame_draw_text(frame, ellipsis, 3, x + (int)line->count * BOX_GLYPH_ADVANCE * sc,
                                y, sc, c);
        }
    }
}

/* The largest rectangle with the image's shape that fits the frame, centred. */
static void fit(const box_scene_t *scene, const box_frame_t *frame, int *x0, int *y0, int *w,
                int *h)
{
    const double sx = (double)frame->width / scene->width;
    const double sy = (double)frame->height / scene->height;
    double k = sx < sy ? sx : sy;
    if (scene->scaling == BOX_SCALING_PIXELATED && k >= 1.0) {
        k = floor(k); /* whole multiples keep every source pixel the same size */
    }
    *w = (int)(scene->width * k + 0.5);
    *h = (int)(scene->height * k + 0.5);
    *x0 = (frame->width - *w) / 2;
    *y0 = (frame->height - *h) / 2;
}

static void draw_image(box_scene_t *scene, box_frame_t *frame, double t)
{
    box_frame_clear(frame, BLACK);
    const uint8_t *src = scene->data + image_index(scene, t) * scene->stride;
    int x0, y0, w, h;
    fit(scene, frame, &x0, &y0, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    const bool smooth = scene->scaling == BOX_SCALING_SMOOTH;
    for (int y = 0; y < h; ++y) {
        uint8_t *out = frame->pixels + ((size_t)(y0 + y) * (size_t)frame->width + (size_t)x0) * 3;
        if (!smooth) {
            const int sy = y * scene->height / h;
            for (int x = 0; x < w; ++x, out += 3) {
                const int sx = x * scene->width / w;
                memcpy(out, src + ((size_t)sy * (size_t)scene->width + (size_t)sx) * 3, 3);
            }
            continue;
        }
        const float fy = ((y + 0.5f) * scene->height / h) - 0.5f;
        const int ya = fy < 0 ? 0 : (int)fy;
        const int yb = ya + 1 < scene->height ? ya + 1 : ya;
        const float wy = fy < 0 ? 0 : fy - ya;
        for (int x = 0; x < w; ++x, out += 3) {
            const float fx = ((x + 0.5f) * scene->width / w) - 0.5f;
            const int xa = fx < 0 ? 0 : (int)fx;
            const int xb = xa + 1 < scene->width ? xa + 1 : xa;
            const float wx = fx < 0 ? 0 : fx - xa;
            const uint8_t *p00 = src + ((size_t)ya * scene->width + xa) * 3;
            const uint8_t *p01 = src + ((size_t)ya * scene->width + xb) * 3;
            const uint8_t *p10 = src + ((size_t)yb * scene->width + xa) * 3;
            const uint8_t *p11 = src + ((size_t)yb * scene->width + xb) * 3;
            for (int c = 0; c < 3; ++c) {
                const float top = p00[c] + (p01[c] - p00[c]) * wx;
                const float bottom = p10[c] + (p11[c] - p10[c]) * wx;
                out[c] = (uint8_t)(top + (bottom - top) * wy + 0.5f);
            }
        }
    }
}

void box_scene_render(box_scene_t *scene, box_frame_t *frame, double t, box_style_t style)
{
    switch (scene->content) {
    case BOX_CONTENT_COLOR:
        box_frame_clear(frame, scene->color);
        break;
    case BOX_CONTENT_IMAGE:
        draw_image(scene, frame, t);
        break;
    case BOX_CONTENT_CLOCK:
        update_clock(scene);
        /* a clock is text */
        __attribute__((fallthrough));
    case BOX_CONTENT_TEXT:
        box_frame_clear(frame, BLACK);
        if (style == BOX_STYLE_LED) {
            draw_text_led(scene, frame);
        } else {
            draw_text_smooth(scene, frame);
        }
        break;
    case BOX_CONTENT_CLEAR:
    default:
        box_frame_clear(frame, BLACK);
        break;
    }
}
