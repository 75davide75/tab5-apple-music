#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Enlarges a tiny RGB565 image (the blurred cover colours) into a smooth full-screen
 * RGB565 background. Smooth interpolation hides the source grid and dithering hides the
 * steps between the few shades RGB565 can show. `keep255` darkens the result (255 = as is).
 * Returns false, leaving dst untouched, on bad arguments or if it runs out of memory. */
bool bg_upscale(const uint16_t *src, int w, int h, uint16_t *dst, int dw, int dh, int keep255);
