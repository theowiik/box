// Smooth text with Inter through stb_truetype. Built with warnings off (see
// CMakeLists.txt): stb_truetype is third-party code.
#include "box/font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"

#include "fonts.inc"

/* Margin around a card, as a share of the screen's width. */
#define MARGIN 0.07f
/* Line height as a multiple of the font size. */
#define LINE 1.18f
/* Body sizes tried, in pixels: the largest that fits wins. */
#define BODY_MAX_SHARE 0.6f
#define BODY_MIN 18.0f
#define BODY_STEP 2.0f
/* The title: a share of the body size, within bounds, at most two lines. */
#define TITLE_SHARE 0.42f
#define TITLE_MIN 26.0f
#define TITLE_MAX 52.0f
#define TITLE_LINES 2
#define ELLIPSIS 0x2026u
/* Widths are measured once at this size and scaled: glyph advances and
 * kerning are linear in the size. */
#define REF 100.0f
#define TEXT_MAX 600

static struct {
    bool ready;
    stbtt_fontinfo face[2];
    unsigned char *scratch;
    size_t scratch_size;
} s;

static stbtt_fontinfo *face(box_font_face_t which)
{
    if (!s.ready) {
        stbtt_InitFont(&s.face[BOX_FONT_BOLD], box_font_bold, 0);
        stbtt_InitFont(&s.face[BOX_FONT_MEDIUM], box_font_medium, 0);
        s.ready = true;
    }
    return &s.face[which];
}

static uint32_t drawable(const stbtt_fontinfo *f, uint32_t cp)
{
    return stbtt_FindGlyphIndex(f, (int)cp) ? cp : '?';
}

/* Advance of cps[i] plus its kerning with cps[i + 1], at REF. */
static float advance_at_ref(const stbtt_fontinfo *f, const uint32_t *cps, size_t count, size_t i)
{
    const float scale = stbtt_ScaleForPixelHeight(f, REF);
    int adv, lsb;
    const uint32_t cp = drawable(f, cps[i]);
    stbtt_GetCodepointHMetrics(f, (int)cp, &adv, &lsb);
    float w = adv * scale;
    if (i + 1 < count) {
        w += stbtt_GetCodepointKernAdvance(f, (int)cp, (int)drawable(f, cps[i + 1])) * scale;
    }
    return w;
}

int box_font_width(box_font_face_t which, float size, const uint32_t *cps, size_t count)
{
    const stbtt_fontinfo *f = face(which);
    float w = 0;
    for (size_t i = 0; i < count; ++i) {
        w += advance_at_ref(f, cps, count, i);
    }
    return (int)ceilf(w * size / REF);
}

void box_font_draw(box_frame_t *frame, box_font_face_t which, float size, const uint32_t *cps,
                   size_t count, int x, int baseline, box_rgb_t color)
{
    const stbtt_fontinfo *f = face(which);
    const float scale = stbtt_ScaleForPixelHeight(f, size);
    float pen = (float)x;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t cp = drawable(f, cps[i]);
        const float shift = pen - floorf(pen);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(f, (int)cp, scale, scale, shift, 0, &x0, &y0, &x1,
                                            &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            const size_t need = (size_t)w * (size_t)h;
            if (need > s.scratch_size) {
                unsigned char *grown = realloc(s.scratch, need);
                if (!grown) {
                    return;
                }
                s.scratch = grown;
                s.scratch_size = need;
            }
            stbtt_MakeCodepointBitmapSubpixel(f, s.scratch, w, h, w, scale, scale, shift, 0,
                                              (int)cp);
            const int gx = (int)floorf(pen) + x0, gy = baseline + y0;
            for (int row = 0; row < h; ++row) {
                for (int col = 0; col < w; ++col) {
                    const unsigned a = s.scratch[row * w + col];
                    if (a) {
                        box_frame_blend_px(frame, gx + col, gy + row, color, a / 255.0f);
                    }
                }
            }
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f, (int)cp, &adv, &lsb);
        pen += adv * scale;
        if (i + 1 < count) {
            pen += stbtt_GetCodepointKernAdvance(f, (int)cp, (int)drawable(f, cps[i + 1])) * scale;
        }
    }
}

/* --- card layout ----------------------------------------------------------- */

typedef struct {
    const uint32_t *cps;
    size_t count;
    float adv[TEXT_MAX]; /* per code point at REF, kerning with the next included */
} measured_t;

static void measure(measured_t *m, box_font_face_t which, const uint32_t *cps, size_t count)
{
    const stbtt_fontinfo *f = face(which);
    m->cps = cps;
    m->count = count < TEXT_MAX ? count : TEXT_MAX;
    for (size_t i = 0; i < m->count; ++i) {
        m->adv[i] = advance_at_ref(f, cps, m->count, i);
    }
}

/* Width of [start, end) at REF; the last glyph's kerning with what follows
 * the range does not count. */
static float span(const measured_t *m, size_t start, size_t end)
{
    float w = 0;
    for (size_t i = start; i < end; ++i) {
        w += m->adv[i];
    }
    return w;
}

/* Greedy word wrap at `size` into lines no wider than `width`. Returns how
 * far it got: m->count when everything fit. Without `split`, a word too long
 * for a line stops the wrap instead of breaking. */
static size_t wrap(const measured_t *m, float size, float width, size_t max_lines, bool split,
                   box_card_line_t *lines, size_t *line_count)
{
    const float limit = width * REF / size; /* the width, in REF units */
    size_t i = 0, n = 0;
    while (i < m->count && n < max_lines) {
        while (i < m->count && m->cps[i] == ' ') {
            ++i;
        }
        if (i == m->count) {
            break;
        }
        const size_t start = i;
        size_t end = start, last_fit_word = start;
        float w = 0;
        bool forced = false;
        while (end < m->count) {
            if (m->cps[end] == '\n') {
                forced = true;
                break;
            }
            if (w + m->adv[end] > limit) {
                break;
            }
            w += m->adv[end];
            ++end;
            if (end == m->count || m->cps[end] == ' ' || m->cps[end] == '\n') {
                last_fit_word = end;
            }
        }
        size_t line_end;
        if (forced || end == m->count) {
            line_end = end;
            i = forced ? end + 1 : end;
        } else if (last_fit_word > start) {
            line_end = last_fit_word;
            i = last_fit_word;
        } else if (split && end > start) {
            line_end = end;
            i = end;
        } else {
            i = start;
            break;
        }
        while (line_end > start && m->cps[line_end - 1] == ' ') {
            --line_end;
        }
        lines[n++] = (box_card_line_t){start, line_end - start};
    }
    while (i < m->count && (m->cps[i] == ' ' || m->cps[i] == '\n')) {
        ++i;
    }
    *line_count = n;
    return i;
}

static float title_size_for(float body)
{
    const float t = body * TITLE_SHARE;
    return t < TITLE_MIN ? TITLE_MIN : t > TITLE_MAX ? TITLE_MAX : t;
}

void box_card_layout(const uint32_t *title, size_t title_count, const uint32_t *body,
                     size_t body_count, int width, int height, box_card_layout_t *out)
{
    memset(out, 0, sizeof(*out));
    static measured_t mt, mb; /* render task only; too large for its stack */
    measure(&mt, BOX_FONT_MEDIUM, title, title_count);
    measure(&mb, BOX_FONT_BOLD, body, body_count);
    const float margin = width * MARGIN;
    const float w = width - 2 * margin, h = height - 2 * margin;
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool split = pass == 1;
        for (float size = h * BODY_MAX_SHARE; size >= BODY_MIN; size -= BODY_STEP) {
            const float ts = mt.count ? title_size_for(size) : 0;
            size_t tn = 0, bn = 0;
            if (mt.count) {
                wrap(&mt, ts, w, TITLE_LINES, true, out->title, &tn);
            }
            const float title_h = tn ? tn * ts * LINE + ts * 0.5f : 0;
            const size_t rows = (size_t)((h - title_h) / (size * LINE));
            const size_t max_rows = rows < BOX_CARD_LINES_MAX ? rows : BOX_CARD_LINES_MAX;
            if (max_rows == 0) {
                continue;
            }
            const size_t reached = wrap(&mb, size, w, max_rows, split, out->body, &bn);
            const bool last_chance = split && size - BODY_STEP < BODY_MIN;
            if (reached == mb.count || last_chance) {
                out->title_size = ts;
                out->body_size = size;
                out->title_lines = tn;
                out->body_lines = bn;
                out->truncated = reached < mb.count;
                if (out->truncated && bn) {
                    /* Make room for "…" on the last line. */
                    const uint32_t dots[] = {ELLIPSIS};
                    const float room = w * REF / size -
                                       box_font_width(BOX_FONT_BOLD, REF, dots, 1);
                    box_card_line_t *last = &out->body[bn - 1];
                    while (last->count && span(&mb, last->start, last->start + last->count) > room) {
                        --last->count;
                    }
                }
                return;
            }
        }
    }
}

/* Distance from a line's top to its baseline at `size`. */
static float ascent(box_font_face_t which, float size)
{
    const stbtt_fontinfo *f = face(which);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    return asc * stbtt_ScaleForPixelHeight(f, size);
}

static int line_x(int width, int line_w, box_align_t align)
{
    const int margin = (int)(width * MARGIN);
    return align == BOX_ALIGN_LEFT ? margin : (width - line_w) / 2;
}

void box_card_draw(box_frame_t *frame, const box_card_layout_t *layout, const uint32_t *title,
                   const uint32_t *body, box_align_t align, box_rgb_t accent)
{
    static const box_rgb_t white = {255, 255, 255};
    static const uint32_t ellipsis[] = {ELLIPSIS};
    const float ts = layout->title_size, bs = layout->body_size;
    const float title_h = layout->title_lines ? layout->title_lines * ts * LINE + ts * 0.5f : 0;
    const float block = title_h + layout->body_lines * bs * LINE;
    float top = (frame->height - block) / 2.0f;
    for (size_t i = 0; i < layout->title_lines; ++i, top += ts * LINE) {
        const box_card_line_t *l = &layout->title[i];
        const int lw = box_font_width(BOX_FONT_MEDIUM, ts, title + l->start, l->count);
        box_font_draw(frame, BOX_FONT_MEDIUM, ts, title + l->start, l->count,
                      line_x(frame->width, lw, align), (int)(top + ascent(BOX_FONT_MEDIUM, ts)), accent);
    }
    if (layout->title_lines) {
        top += ts * 0.5f;
    }
    const box_rgb_t body_color = layout->title_lines ? white : accent;
    for (size_t i = 0; i < layout->body_lines; ++i, top += bs * LINE) {
        const box_card_line_t *l = &layout->body[i];
        const bool cut = layout->truncated && i + 1 == layout->body_lines;
        int lw = box_font_width(BOX_FONT_BOLD, bs, body + l->start, l->count);
        const int ew = cut ? box_font_width(BOX_FONT_BOLD, bs, ellipsis, 1) : 0;
        const int x = line_x(frame->width, lw + ew, align);
        const int baseline = (int)(top + ascent(BOX_FONT_BOLD, bs));
        box_font_draw(frame, BOX_FONT_BOLD, bs, body + l->start, l->count, x, baseline, body_color);
        if (cut) {
            box_font_draw(frame, BOX_FONT_BOLD, bs, ellipsis, 1, x + lw, baseline, body_color);
        }
    }
}
