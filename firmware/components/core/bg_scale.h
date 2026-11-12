#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Enlarges a tiny RGB565 image (the blurred cover colours) into a smooth full-screen
 * RGB565 background. Smooth interpolation hides the source grid and dithering hides the
 * steps between the few shades RGB565 can show. `keep255` darkens the result (255 = as is).
 * `shadow` and `flat` may be NULL.
 * Returns false, leaving dst untouched, on bad arguments or if it runs out of memory. */
/* A soft shadow under a rounded rectangle (the cover), drawn into the background so it gets
 * the same dithering: full strength under the rectangle, fading to nothing `blur` pixels out. */
typedef struct {
    int x, y, w, h;     /* the rectangle casting the shadow, in destination pixels */
    int radius;         /* its corner radius */
    int blur;           /* how far the shadow reaches beyond the rectangle */
    int opa255;         /* darkness at full strength, 0..255 */
} bg_shadow_t;

/* Makes everything from column x0 rightwards one single colour (the average of the picture
 * there), with the picture melting into it over the `fade` columns before x0. An opaque
 * panel of exactly that colour can then sit on top and be scrolled without the picture
 * underneath ever being redrawn. `colour` is filled in. */
typedef struct {
    int x0, fade;
    uint16_t colour;    /* out: the RGB565 colour used */
} bg_flat_t;

bool bg_upscale(const uint16_t *src, int w, int h, uint16_t *dst, int dw, int dh, int keep255,
                const bg_shadow_t *shadow, bg_flat_t *flat);
