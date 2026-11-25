#include "base64.h"

static int value_of(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static size_t padding(const char *text, size_t len)
{
    /* Only trailing '=' count: "AB=C" has none, and is rejected by decode. */
    if (len < 1 || text[len - 1] != '=') {
        return 0;
    }
    return len >= 2 && text[len - 2] == '=' ? 2 : 1;
}

size_t box_base64_decoded_size(const char *text, size_t len)
{
    if (len == 0 || len % 4) {
        return 0;
    }
    return len / 4 * 3 - padding(text, len);
}

bool box_base64_decode(const char *text, size_t len, uint8_t *out)
{
    if (len == 0 || len % 4) {
        return false;
    }
    const size_t pad = padding(text, len);
    size_t o = 0;
    for (size_t i = 0; i < len; i += 4) {
        const bool last = i + 4 == len;
        int v[4];
        for (int k = 0; k < 4; ++k) {
            const bool is_pad = last && k >= 4 - (int)pad;
            v[k] = is_pad ? 0 : value_of(text[i + (size_t)k]);
            if (v[k] < 0) {
                return false; /* Includes '=' anywhere but the end. */
            }
        }
        const uint32_t n = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 |
                           (uint32_t)v[2] << 6 | (uint32_t)v[3];
        out[o++] = (uint8_t)(n >> 16);
        if (!last || pad < 2) {
            out[o++] = (uint8_t)(n >> 8);
        }
        if (!last || pad < 1) {
            out[o++] = (uint8_t)n;
        }
    }
    return true;
}
