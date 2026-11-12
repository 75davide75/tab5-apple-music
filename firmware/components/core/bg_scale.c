#include "bg_scale.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/* Working memory. On the tablet it must come from the external RAM: measured on this board,
 * a loop over internal RAM ran about 30 times slower than the same loop over external RAM. */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *work_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
}
#else
static void *work_alloc(size_t n) { return malloc(n); }
#endif

/* Everything per pixel is integer arithmetic and table look-ups: the float version of this
 * took seconds on the tablet and froze the screen at every track change. */

/* Cubic B-spline taps for one output coordinate: four source indices and their weights,
 * which add up to exactly 4096. Unlike straight-line blending it has no creases at the
 * source grid and never overshoots. */
typedef struct { uint8_t idx[4]; int16_t wt[4]; } taps_t;

static void make_taps(taps_t *t, int out, int out_n, int src_n)
{
    float f = ((float)out + 0.5f) * (float)src_n / (float)out_n - 0.5f;
    int i = (int)f;
    if ((float)i > f) i--;                               /* floor, also below zero */
    float u = f - (float)i, u2 = u * u, u3 = u2 * u;
    int w0 = (int)((1.0f - 3.0f * u + 3.0f * u2 - u3) / 6.0f * 4096.0f + 0.5f);
    int w2 = (int)((-3.0f * u3 + 3.0f * u2 + 3.0f * u + 1.0f) / 6.0f * 4096.0f + 0.5f);
    int w3 = (int)(u3 / 6.0f * 4096.0f + 0.5f);
    t->wt[0] = (int16_t)w0;
    t->wt[2] = (int16_t)w2;
    t->wt[3] = (int16_t)w3;
    t->wt[1] = (int16_t)(4096 - w0 - w2 - w3);
    for (int k = 0; k < 4; k++) {
        int j = i - 1 + k;
        t->idx[k] = (uint8_t)(j < 0 ? 0 : (j >= src_n ? src_n - 1 : j));
    }
}

/* Evenly spread noise, 1..254, with no visible pattern (interleaved gradient noise). Staying
 * inside the 0..255 step keeps an exact shade, black and white included, exact. */
static int dither_at(int x, int y)
{
    float v = 52.9829189f * (0.06711056f * (float)x + 0.00583715f * (float)y);
    v -= (float)(int)v;
    return 1 + (int)(v * 253.99f);
}

/* The shadow as two tables: distance for a squared distance, and light kept for a distance. */
typedef struct {
    const bg_shadow_t *s;
    int reach;              /* radius + blur */
    uint8_t *root;          /* root[a*a + b*b] = distance, for a, b <= reach */
    uint8_t keep[256];      /* keep[d] = light left (0..255) d pixels outside the rectangle */
} shadow_tab_t;

static bool shadow_prepare(shadow_tab_t *t, const bg_shadow_t *s)
{
    t->s = s;
    t->reach = s->radius + s->blur;
    if (s->radius < 0 || s->blur < 0 || t->reach > 250 || s->w < 2 * s->radius || s->h < 2 * s->radius) return false;
    int n = 2 * t->reach * t->reach + 1;
    t->root = work_alloc((size_t)n);
    if (!t->root) return false;
    for (int i = 0, d = 0; i < n; i++) {
        while ((d + 1) * (d + 1) <= i) d++;
        t->root[i] = (uint8_t)(d > 255 ? 255 : d);
    }
    for (int d = 0; d < 256; d++) {
        float fade = 0;
        if (d == 0) fade = 1;
        else if (d < s->blur) {
            float u = (float)d / (float)s->blur;
            fade = 1.0f - u * u * (3.0f - 2.0f * u);     /* eases in and out: no rim, no hard end */
        }
        t->keep[d] = (uint8_t)(255 - (int)((float)s->opa255 * fade + 0.5f));
    }
    return true;
}

/* How much light the shadow leaves at a pixel, 0..255 (255 = none taken away). */
static inline int shadow_keep(const shadow_tab_t *t, int px, int py)
{
    const bg_shadow_t *s = t->s;
    /* Offsets beyond the rectangle shrunk by its corner radius, in whole pixels. */
    int ax = 2 * px + 1 - (2 * s->x + s->w), ay = 2 * py + 1 - (2 * s->y + s->h);
    if (ax < 0) ax = -ax;
    if (ay < 0) ay = -ay;
    int ox = (ax - (s->w - 2 * s->radius)) / 2, oy = (ay - (s->h - 2 * s->radius)) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    if (ox > t->reach || oy > t->reach) return 255;
    int d = (int)t->root[ox * ox + oy * oy] - s->radius;
    return t->keep[d < 0 ? 0 : d];
}

bool bg_upscale(const uint16_t *src, int w, int h, uint16_t *dst, int dw, int dh, int keep255,
                const bg_shadow_t *shadow, bg_flat_t *flat)
{
    if (!src || !dst || w < 2 || h < 2 || w > 255 || h > 255 || dw < 2 || dh < 2) return false;
    shadow_tab_t tab = {0};
    if (shadow && !shadow_prepare(&tab, shadow)) shadow = NULL;       /* odd shadow: draw none */
    taps_t *xt = work_alloc(sizeof(taps_t) * (size_t)dw);
    /* Each source row widened to the output width; a channel is its 5- or 6-bit value x 256. */
    uint16_t *rows = work_alloc(sizeof(uint16_t) * 3 * (size_t)dw * (size_t)h);
    if (!xt || !rows) { free(xt); free(rows); free(tab.root); return false; }

    for (int x = 0; x < dw; x++) make_taps(&xt[x], x, dw, w);
    for (int y = 0; y < h; y++) {
        uint16_t *out = rows + (size_t)y * (size_t)dw * 3;
        const uint16_t *in = src + y * w;
        for (int x = 0; x < dw; x++) {
            int r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                int p = in[xt[x].idx[k]], wt = xt[x].wt[k];
                r += wt * ((p >> 11) & 31);
                g += wt * ((p >> 5) & 63);
                b += wt * (p & 31);
            }
            out[x * 3] = (uint16_t)(r >> 4);
            out[x * 3 + 1] = (uint16_t)(g >> 4);
            out[x * 3 + 2] = (uint16_t)(b >> 4);
        }
    }
    /* The flat side: its colour is the average of the widened rows there, and blend[] says
     * how much of it each column of the join takes (0..256). */
    int flat_c[3] = {0, 0, 0}, flat_x0 = dw, join_x0 = dw;
    uint16_t flat_px = 0;
    uint16_t *blend = NULL;
    if (flat && flat->x0 >= 0 && flat->x0 < dw && flat->fade >= 0) {
        flat_x0 = flat->x0;
        join_x0 = flat_x0 - flat->fade;
        blend = flat->fade > 0 ? work_alloc(sizeof(uint16_t) * (size_t)flat->fade) : NULL;
        if (flat->fade > 0 && !blend) join_x0 = flat_x0;        /* out of memory: a hard join */
        for (int i = 0; blend && i < flat->fade; i++) {
            float u = ((float)i + 0.5f) / (float)flat->fade;
            blend[i] = (uint16_t)(u * u * (3.0f - 2.0f * u) * 256.0f + 0.5f);
        }
        int64_t sum[3] = {0, 0, 0};
        for (int y = 0; y < h; y++) {
            const uint16_t *row = rows + (size_t)y * (size_t)dw * 3;
            for (int x = flat_x0; x < dw; x++) for (int k = 0; k < 3; k++) sum[k] += row[x * 3 + k];
        }
        int64_t n = (int64_t)h * (dw - flat_x0);
        int q[3];
        for (int k = 0; k < 3; k++) {
            int v = (int)(sum[k] / n);
            if (keep255 < 255) v = (int)(((int64_t)v * keep255 * 257 + 32768) >> 16);
            q[k] = (v + 128) >> 8;                              /* nearest shade the screen has */
            flat_c[k] = q[k] << 8;
        }
        flat_px = (uint16_t)((q[0] << 11) | (q[1] << 5) | q[2]);
        flat->colour = flat_px;
    }
    int sx0 = dw, sx1 = 0, sy0 = dh, sy1 = 0;           /* the only pixels the shadow can touch */
    if (shadow) {
        sx0 = shadow->x - shadow->blur; sx1 = shadow->x + shadow->w + shadow->blur;
        sy0 = shadow->y - shadow->blur; sy1 = shadow->y + shadow->h + shadow->blur;
    }
    for (int y = 0; y < dh; y++) {
        taps_t yt;
        make_taps(&yt, y, dh, h);
        const uint16_t *r0 = rows + (size_t)yt.idx[0] * (size_t)dw * 3, *r1 = rows + (size_t)yt.idx[1] * (size_t)dw * 3;
        const uint16_t *r2 = rows + (size_t)yt.idx[2] * (size_t)dw * 3, *r3 = rows + (size_t)yt.idx[3] * (size_t)dw * 3;
        const int w0 = yt.wt[0], w1 = yt.wt[1], w2 = yt.wt[2], w3 = yt.wt[3];
        const bool shadow_row = shadow && y >= sy0 && y < sy1;
        uint16_t *out = dst + (size_t)y * (size_t)dw;
        for (int x = flat_x0; x < dw; x++) out[x] = flat_px;
        for (int x = 0; x < flat_x0; x++) {
            int i = x * 3;
            int c[3];
            for (int k = 0; k < 3; k++) {
                c[k] = (w0 * r0[i + k] + w1 * r1[i + k] + w2 * r2[i + k] + w3 * r3[i + k]) >> 12;
            }
            int keep = keep255;
            if (shadow_row && x >= sx0 && x < sx1) keep = (keep * shadow_keep(&tab, x, y) + 127) / 255;
            if (keep < 255) {
                for (int k = 0; k < 3; k++) c[k] = (int)(((int64_t)c[k] * keep * 257 + 32768) >> 16);
            }
            if (x >= join_x0) {
                int t = blend[x - join_x0];
                for (int k = 0; k < 3; k++) c[k] += ((flat_c[k] - c[k]) * t) >> 8;
            }
            int n = dither_at(x, y);
            /* Opposite noise on green so the three channels do not brighten together. */
            int r = (c[0] + n) >> 8, g = (c[1] + 255 - n) >> 8, b = (c[2] + n) >> 8;
            out[x] = (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
        }
    }
    free(xt);
    free(rows);
    free(tab.root);
    free(blend);
    return true;
}
