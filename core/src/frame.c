#include "box/frame.h"

#include <stdlib.h>
#include <string.h>

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool box_rgb_parse(const char *text, box_rgb_t *out)
{
    if (!text || !out) {
        return false;
    }
    if (*text == '#') {
        ++text;
    }
    const size_t len = strlen(text);
    if (len != 3 && len != 6) {
        return false;
    }
    int nibbles[6];
    for (size_t i = 0; i < 6; ++i) {
        /* "RGB" doubles each digit, like CSS. */
        const int d = hex_digit(text[len == 3 ? i / 2 : i]);
        if (d < 0) {
            return false;
        }
        nibbles[i] = d;
    }
    out->r = (uint8_t)(nibbles[0] << 4 | nibbles[1]);
    out->g = (uint8_t)(nibbles[2] << 4 | nibbles[3]);
    out->b = (uint8_t)(nibbles[4] << 4 | nibbles[5]);
    return true;
}

void box_rgb_format(box_rgb_t color, char out[8])
{
    static const char digits[] = "0123456789ABCDEF";
    const uint8_t parts[3] = {color.r, color.g, color.b};
    out[0] = '#';
    for (int i = 0; i < 3; ++i) {
        out[1 + i * 2] = digits[parts[i] >> 4];
        out[2 + i * 2] = digits[parts[i] & 0xF];
    }
    out[7] = '\0';
}

bool box_frame_init(box_frame_t *frame, int width, int height)
{
    memset(frame, 0, sizeof(*frame));
    if (width < 1 || height < 1) {
        return false;
    }
    frame->pixels = calloc((size_t)width * (size_t)height, 3);
    if (!frame->pixels) {
        return false;
    }
    frame->width = width;
    frame->height = height;
    box_frame_changed_all(frame);
    return true;
}

void box_frame_changed_all(box_frame_t *frame)
{
    frame->changed = (box_rect_t){0, 0, frame->width - 1, frame->height - 1};
}

void box_frame_free(box_frame_t *frame)
{
    free(frame->pixels);
    memset(frame, 0, sizeof(*frame));
}

size_t box_frame_bytes(const box_frame_t *frame)
{
    return (size_t)frame->width * (size_t)frame->height * 3;
}

void box_frame_clear(box_frame_t *frame, box_rgb_t color)
{
    const size_t count = (size_t)frame->width * (size_t)frame->height;
    if (color.r == 0 && color.g == 0 && color.b == 0) {
        memset(frame->pixels, 0, count * 3);
        return;
    }
    uint8_t *p = frame->pixels;
    for (size_t i = 0; i < count; ++i) {
        *p++ = color.r;
        *p++ = color.g;
        *p++ = color.b;
    }
}

void box_frame_set_px(box_frame_t *frame, int x, int y, box_rgb_t color)
{
    if (x < 0 || y < 0 || x >= frame->width || y >= frame->height) {
        return;
    }
    uint8_t *p = frame->pixels + ((size_t)y * (size_t)frame->width + (size_t)x) * 3;
    p[0] = color.r;
    p[1] = color.g;
    p[2] = color.b;
}

void box_frame_blend_px(box_frame_t *frame, int x, int y, box_rgb_t color, float alpha)
{
    if (alpha <= 0.0f || x < 0 || y < 0 || x >= frame->width || y >= frame->height) {
        return;
    }
    if (alpha >= 1.0f) {
        box_frame_set_px(frame, x, y, color);
        return;
    }
    uint8_t *p = frame->pixels + ((size_t)y * (size_t)frame->width + (size_t)x) * 3;
    const float inv = 1.0f - alpha;
    p[0] = (uint8_t)(p[0] * inv + color.r * alpha);
    p[1] = (uint8_t)(p[1] * inv + color.g * alpha);
    p[2] = (uint8_t)(p[2] * inv + color.b * alpha);
}
