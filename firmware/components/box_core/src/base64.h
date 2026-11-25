// Strict standard base64 (RFC 4648 alphabet, '=' padding required).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Decoded size of `len` characters of valid base64, or 0 if the length is wrong. */
size_t box_base64_decoded_size(const char *text, size_t len);

/** Decode exactly box_base64_decoded_size() bytes into out. Returns false on
 *  any character outside the alphabet or misplaced padding. */
bool box_base64_decode(const char *text, size_t len, uint8_t *out);
