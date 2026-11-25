#include "box/scene.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "box/text.h"

struct box_scene {
    int refs;
    box_scene_kind_t kind;
    box_rgb_t color;
    /* Text: the original UTF-8, its code points, and the layout for the last
     * panel size it was drawn at. */
    char *text;
    uint32_t *codepoints;
    size_t codepoint_count;
    box_text_layout_t *layout;
    int layout_w, layout_h;
    /* Frames: flat RGB, then a scaling map rebuilt when the panel size differs. */
    uint8_t *data;
    size_t size;
    int width, height;
    size_t stride, count;
    float fps;
    bool loop;
    uint32_t *map;
    int map_w, map_h;
};

static atomic_size_t s_frames_in_use;
static size_t s_frames_budget = 12u * 1024u * 1024u;

void box_frames_set_budget(size_t bytes)
{
    s_frames_budget = bytes;
}

bool box_frames_budget_allows(size_t bytes)
{
    const size_t used = atomic_load(&s_frames_in_use);
    return bytes <= s_frames_budget && used <= s_frames_budget - bytes;
}

size_t box_frames_in_use(void)
{
    return atomic_load(&s_frames_in_use);
}

static box_scene_t *alloc_scene(box_scene_kind_t kind)
{
    box_scene_t *scene = calloc(1, sizeof(*scene));
    if (scene) {
        scene->refs = 1;
        scene->kind = kind;
    }
    return scene;
}

box_scene_t *box_scene_new_blank(void)
{
    return alloc_scene(BOX_SCENE_BLANK);
}

box_scene_t *box_scene_new_solid(box_rgb_t color)
{
    box_scene_t *scene = alloc_scene(BOX_SCENE_SOLID);
    if (scene) {
        scene->color = color;
    }
    return scene;
}

box_scene_t *box_scene_new_text(const char *utf8, size_t len, box_rgb_t color)
{
    box_scene_t *scene = alloc_scene(BOX_SCENE_TEXT);
    if (!scene) {
        return NULL;
    }
    const size_t count = box_utf8_count(utf8, len);
    scene->text = malloc(len + 1);
    scene->codepoints = malloc((count ? count : 1) * sizeof(uint32_t));
    if (!scene->text || !scene->codepoints) {
        box_scene_release(scene);
        return NULL;
    }
    memcpy(scene->text, utf8, len);
    scene->text[len] = '\0';
    scene->codepoint_count = box_utf8_decode(utf8, len, scene->codepoints, count);
    scene->color = color;
    return scene;
}

box_scene_t *box_scene_new_frames(uint8_t *data, size_t size, int width, int height,
                                  float fps, bool loop)
{
    const size_t stride = width > 0 && height > 0 ? (size_t)width * (size_t)height * 3 : 0;
    if (!data || stride == 0 || size == 0 || size % stride) {
        free(data);
        return NULL;
    }
    box_scene_t *scene = alloc_scene(BOX_SCENE_FRAMES);
    if (!scene) {
        free(data);
        return NULL;
    }
    scene->data = data;
    scene->size = size;
    scene->width = width;
    scene->height = height;
    scene->stride = stride;
    scene->count = size / stride;
    scene->fps = fps > 0.1f ? fps : 0.1f;
    scene->loop = loop;
    atomic_fetch_add(&s_frames_in_use, size);
    return scene;
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
        atomic_fetch_sub(&s_frames_in_use, scene->size);
    }
    free(scene->text);
    free(scene->codepoints);
    free(scene->layout);
    free(scene->data);
    free(scene->map);
    free(scene);
}

box_scene_kind_t box_scene_kind(const box_scene_t *scene)
{
    return scene->kind;
}

const char *box_scene_kind_name(const box_scene_t *scene)
{
    switch (scene->kind) {
    case BOX_SCENE_SOLID:
        return "solid";
    case BOX_SCENE_TEXT:
        return "text";
    case BOX_SCENE_FRAMES:
        return "frames";
    case BOX_SCENE_BLANK:
    default:
        return "blank";
    }
}

const char *box_scene_text(const box_scene_t *scene)
{
    return scene->kind == BOX_SCENE_TEXT ? scene->text : NULL;
}

static const box_text_layout_t *text_layout(box_scene_t *scene, int w, int h)
{
    if (scene->layout && scene->layout_w == w && scene->layout_h == h) {
        return scene->layout;
    }
    if (!scene->layout) {
        scene->layout = malloc(sizeof(*scene->layout));
        if (!scene->layout) {
            return NULL;
        }
    }
    box_text_layout(scene->codepoints, scene->codepoint_count, w, h, scene->layout);
    scene->layout_w = w;
    scene->layout_h = h;
    return scene->layout;
}

static void render_text(box_scene_t *scene, box_frame_t *frame)
{
    static const box_rgb_t black = {0, 0, 0};
    static const uint32_t ellipsis[] = {'.', '.', '.'};
    box_frame_clear(frame, black);
    const box_text_layout_t *layout = text_layout(scene, frame->width, frame->height);
    if (!layout || layout->line_count == 0) {
        return;
    }
    const int s = layout->scale;
    const int block = (int)layout->line_count * BOX_LINE_ADVANCE * s - s;
    int y = (frame->height - block) / 2;
    for (size_t i = 0; i < layout->line_count; ++i, y += BOX_LINE_ADVANCE * s) {
        const box_text_line_t *line = &layout->lines[i];
        const bool cut = layout->truncated && i + 1 == layout->line_count;
        const size_t glyphs = line->count + (cut ? 3 : 0);
        const int x = (frame->width - box_text_width(glyphs, s)) / 2;
        box_frame_draw_text(frame, scene->codepoints + line->start, line->count, x, y, s,
                            scene->color);
        if (cut) {
            box_frame_draw_text(frame, ellipsis, 3,
                                x + (int)line->count * BOX_GLYPH_ADVANCE * s, y, s,
                                scene->color);
        }
    }
}

/* Byte offsets into one source frame for each destination pixel. */
static const uint32_t *scale_map(box_scene_t *scene, int w, int h)
{
    if (scene->map && scene->map_w == w && scene->map_h == h) {
        return scene->map;
    }
    uint32_t *map = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!map) {
        return NULL;
    }
    for (int y = 0; y < h; ++y) {
        int sy = y * scene->height / h;
        sy = sy < scene->height ? sy : scene->height - 1;
        for (int x = 0; x < w; ++x) {
            int sx = x * scene->width / w;
            sx = sx < scene->width ? sx : scene->width - 1;
            map[y * w + x] = (uint32_t)((sy * scene->width + sx) * 3);
        }
    }
    free(scene->map);
    scene->map = map;
    scene->map_w = w;
    scene->map_h = h;
    return map;
}

static void render_frames(box_scene_t *scene, box_frame_t *frame, double t)
{
    size_t index = t > 0 ? (size_t)(t * scene->fps) : 0;
    if (index >= scene->count) {
        index = scene->loop ? index % scene->count : scene->count - 1;
    }
    const uint8_t *src = scene->data + index * scene->stride;
    if (frame->width == scene->width && frame->height == scene->height) {
        memcpy(frame->pixels, src, scene->stride);
        return;
    }
    const uint32_t *map = scale_map(scene, frame->width, frame->height);
    if (!map) {
        static const box_rgb_t black = {0, 0, 0};
        box_frame_clear(frame, black);
        return;
    }
    const size_t pixels = (size_t)frame->width * (size_t)frame->height;
    uint8_t *out = frame->pixels;
    for (size_t i = 0; i < pixels; ++i, out += 3) {
        memcpy(out, src + map[i], 3);
    }
}

void box_scene_render(box_scene_t *scene, box_frame_t *frame, double t)
{
    static const box_rgb_t black = {0, 0, 0};
    switch (scene->kind) {
    case BOX_SCENE_SOLID:
        box_frame_clear(frame, scene->color);
        break;
    case BOX_SCENE_TEXT:
        render_text(scene, frame);
        break;
    case BOX_SCENE_FRAMES:
        render_frames(scene, frame, t);
        break;
    case BOX_SCENE_BLANK:
    default:
        box_frame_clear(frame, black);
        break;
    }
}
