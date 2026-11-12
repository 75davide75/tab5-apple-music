#pragma once
#include <stdbool.h>

/* Index of the line being sung at elapsed_s: the last line whose time is <= elapsed_s.
 * times[] must be ascending. Returns -1 before the first line or when n == 0. */
int lyrics_current_line(const float *times, int n, float elapsed_s);

/* Decodes a hex string of little-endian RGB565 pixels. Returns the number of pixels
 * written, or -1 if the string is malformed or holds more than max_px pixels. */
int hex_to_rgb565(const char *hex, unsigned short *out, int max_px);
