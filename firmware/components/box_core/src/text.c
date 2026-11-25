#include "box/text.h"

#include <string.h>
#include <stdbool.h>

typedef struct {
    uint32_t codepoint;
    uint8_t rows[BOX_GLYPH_HEIGHT];
} glyph_t;

static const glyph_t FONT[] = {
#include "font_table.inc"
};

#define FONT_COUNT (sizeof(FONT) / sizeof(FONT[0]))
#define REPLACEMENT 0xFFFDu

static uint32_t to_upper(uint32_t c)
{
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    /* Latin-1 lowercase (à..þ, except ÷) sits 0x20 above its capital. */
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
        return c - 0x20;
    }
    return c;
}

static const uint8_t *lookup(uint32_t c)
{
    size_t lo = 0, hi = FONT_COUNT;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (FONT[mid].codepoint == c) {
            return FONT[mid].rows;
        }
        if (FONT[mid].codepoint < c) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const uint8_t *box_font_glyph(uint32_t codepoint)
{
    const uint8_t *rows = lookup(to_upper(codepoint));
    return rows ? rows : lookup('?');
}

int box_text_width(size_t count, int scale)
{
    return count ? ((int)count * BOX_GLYPH_ADVANCE - 1) * scale : 0;
}

/* One code point from s[0..len), its byte length in *used. Malformed input
 * consumes a single byte and yields U+FFFD, so decoding always progresses. */
static uint32_t next_codepoint(const unsigned char *s, size_t len, size_t *used)
{
    const unsigned char b0 = s[0];
    size_t need;
    uint32_t c, min;
    if (b0 < 0x80) {
        *used = 1;
        return b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        need = 1, c = b0 & 0x1F, min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2, c = b0 & 0x0F, min = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3, c = b0 & 0x07, min = 0x10000;
    } else {
        *used = 1;
        return REPLACEMENT;
    }
    if (need >= len) {
        *used = 1;
        return REPLACEMENT;
    }
    for (size_t i = 1; i <= need; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            *used = 1;
            return REPLACEMENT;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        *used = 1;
        return REPLACEMENT;
    }
    *used = need + 1;
    return c;
}

size_t box_utf8_count(const char *text, size_t len)
{
    size_t count = 0, i = 0, used;
    while (i < len) {
        (void)next_codepoint((const unsigned char *)text + i, len - i, &used);
        i += used;
        ++count;
    }
    return count;
}

size_t box_utf8_decode(const char *text, size_t len, uint32_t *out, size_t max)
{
    size_t count = 0, i = 0, used;
    while (i < len && count < max) {
        out[count++] = next_codepoint((const unsigned char *)text + i, len - i, &used);
        i += used;
    }
    return count;
}

void box_frame_draw_text(box_frame_t *frame, const uint32_t *codepoints, size_t count,
                         int x, int y, int scale, box_rgb_t color)
{
    for (size_t index = 0; index < count; ++index) {
        const int gx = x + (int)index * BOX_GLYPH_ADVANCE * scale;
        if (gx >= frame->width || gx + BOX_GLYPH_WIDTH * scale <= 0) {
            continue;
        }
        const uint8_t *rows = box_font_glyph(codepoints[index]);
        for (int row = 0; row < BOX_GLYPH_HEIGHT; ++row) {
            for (int col = 0; col < BOX_GLYPH_WIDTH; ++col) {
                if (!(rows[row] & (0x10 >> col))) {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        box_frame_set_px(frame, gx + col * scale + dx,
                                         y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

/* Greedy word wrap into at most `max_lines` lines of `cols` glyphs. Returns
 * how far into the text it got: `count` when everything fit. Without
 * `split`, a word longer than a line stops the wrap instead of breaking. */
static size_t wrap(const uint32_t *cps, size_t count, size_t cols, size_t max_lines,
                   bool split, box_text_line_t *lines, size_t *line_count)
{
    size_t i = 0, n = 0;
    while (i < count && n < max_lines) {
        while (i < count && cps[i] == ' ') {
            ++i; /* a wrapped line never starts with a space */
        }
        if (i == count) {
            break;
        }
        const size_t start = i;
        size_t j = i, last_space = 0;
        bool has_space = false;
        while (j < count && cps[j] != '\n' && j - start < cols) {
            if (cps[j] == ' ') {
                last_space = j;
                has_space = true;
            }
            ++j;
        }
        size_t end;
        if (j == count || cps[j] == '\n') {
            end = j;
            i = j < count ? j + 1 : j;
        } else if (cps[j] == ' ') {
            end = j; /* the line is exactly full; the space is the break */
            i = j + 1;
        } else if (has_space && last_space > start) {
            end = last_space;
            i = last_space + 1;
        } else if (split) {
            end = j; /* one word longer than a line: break inside it */
            i = j;
        } else {
            i = start; /* refuse to split a word at this size */
            break;
        }
        while (end > start && cps[end - 1] == ' ') {
            --end;
        }
        lines[n++] = (box_text_line_t){start, end - start};
    }
    while (i < count && (cps[i] == ' ' || cps[i] == '\n')) {
        ++i; /* trailing blanks need no line of their own */
    }
    *line_count = n;
    return i;
}

void box_text_layout(const uint32_t *cps, size_t count, int width, int height,
                     box_text_layout_t *out)
{
    memset(out, 0, sizeof(*out));
    out->scale = 1;
    const int w = width - 2 * BOX_TEXT_MARGIN;
    const int h = height - 2 * BOX_TEXT_MARGIN;
    if (count == 0 || w < BOX_GLYPH_WIDTH || h < BOX_GLYPH_HEIGHT) {
        return;
    }
    /* Largest first: a short word fills the panel, a paragraph gets small.
     * The first pass keeps words whole; only if no size can do that (a word
     * longer than a line even at scale 1) are words split. */
    for (int pass = 0; pass < 2; ++pass) {
        const bool split = pass == 1;
        for (int scale = h / BOX_GLYPH_HEIGHT; scale >= 1; --scale) {
            const size_t cols = (size_t)((w + scale) / (BOX_GLYPH_ADVANCE * scale));
            size_t rows = (size_t)((h + scale) / (BOX_LINE_ADVANCE * scale));
            rows = rows < BOX_TEXT_LINES_MAX ? rows : BOX_TEXT_LINES_MAX;
            if (cols == 0 || rows == 0) {
                continue;
            }
            size_t n;
            const size_t reached = wrap(cps, count, cols, rows, split, out->lines, &n);
            if (reached == count || (split && scale == 1)) {
                out->scale = scale;
                out->line_count = n;
                out->truncated = reached < count;
                if (out->truncated && n > 0) {
                    /* Leave room for "..." on the last line. */
                    box_text_line_t *last = &out->lines[n - 1];
                    const size_t room = cols > 3 ? cols - 3 : 0;
                    last->count = last->count < room ? last->count : room;
                }
                return;
            }
        }
    }
}
