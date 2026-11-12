#include "bg_scale.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

/* Cubic B-spline taps for one output coordinate: four source indices and their weights.
 * Unlike straight-line blending it has no creases at the source grid and never overshoots. */
typedef struct { int idx[4]; float wt[4]; } taps_t;

static void make_taps(taps_t *t, int out, int out_n, int src_n)
{
    float f = ((float)out + 0.5f) * (float)src_n / (float)out_n - 0.5f;
    int i = (int)floorf(f);
    float u = f - (float)i, u2 = u * u, u3 = u2 * u;
    t->wt[0] = (1.0f - 3.0f * u + 3.0f * u2 - u3) / 6.0f;
    t->wt[1] = (3.0f * u3 - 6.0f * u2 + 4.0f) / 6.0f;
    t->wt[2] = (-3.0f * u3 + 3.0f * u2 + 3.0f * u + 1.0f) / 6.0f;
    t->wt[3] = u3 / 6.0f;
    for (int k = 0; k < 4; k++) {
        int j = i - 1 + k;
        t->idx[k] = j < 0 ? 0 : (j >= src_n ? src_n - 1 : j);
    }
}

/* Evenly spread noise in [0,1) with no visible pattern (interleaved gradient noise). */
static float dither_at(int x, int y)
{
    float v = 52.9829189f * (0.06711056f * (float)x + 0.00583715f * (float)y);
    v -= floorf(v);
    return v;
}

static int quantise(float value01, int max, float noise)
{
    /* Noise kept just inside (0,1) so an exact shade, black and white included, stays exact. */
    int q = (int)floorf(value01 * (float)max + 0.001f + noise * 0.998f);
    return q < 0 ? 0 : (q > max ? max : q);
}

bool bg_upscale(const uint16_t *src, int w, int h, uint16_t *dst, int dw, int dh, int keep255)
{
    if (!src || !dst || w < 2 || h < 2 || dw < 2 || dh < 2) return false;
    taps_t *xt = malloc(sizeof(taps_t) * (size_t)dw);
    float *rows = malloc(sizeof(float) * 3 * (size_t)dw * (size_t)h);   /* each source row, widened */
    if (!xt || !rows) { free(xt); free(rows); return false; }

    const float keep = (float)keep255 / 255.0f;
    for (int x = 0; x < dw; x++) make_taps(&xt[x], x, dw, w);
    for (int y = 0; y < h; y++) {
        float *out = rows + (size_t)y * (size_t)dw * 3;
        for (int x = 0; x < dw; x++) {
            float r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                uint16_t p = src[y * w + xt[x].idx[k]];
                r += xt[x].wt[k] * (float)((p >> 11) & 31);
                g += xt[x].wt[k] * (float)((p >> 5) & 63);
                b += xt[x].wt[k] * (float)(p & 31);
            }
            out[x * 3] = r * keep / 31.0f;
            out[x * 3 + 1] = g * keep / 63.0f;
            out[x * 3 + 2] = b * keep / 31.0f;
        }
    }
    for (int y = 0; y < dh; y++) {
        taps_t yt;
        make_taps(&yt, y, dh, h);
        const float *r0 = rows + (size_t)yt.idx[0] * (size_t)dw * 3, *r1 = rows + (size_t)yt.idx[1] * (size_t)dw * 3;
        const float *r2 = rows + (size_t)yt.idx[2] * (size_t)dw * 3, *r3 = rows + (size_t)yt.idx[3] * (size_t)dw * 3;
        for (int x = 0; x < dw; x++) {
            float c[3];
            for (int k = 0; k < 3; k++) {
                int i = x * 3 + k;
                c[k] = yt.wt[0] * r0[i] + yt.wt[1] * r1[i] + yt.wt[2] * r2[i] + yt.wt[3] * r3[i];
            }
            float n = dither_at(x, y);
            /* Opposite noise on green so the three channels do not brighten together. */
            dst[y * dw + x] = (uint16_t)((quantise(c[0], 31, n) << 11) | (quantise(c[1], 63, 1.0f - n) << 5)
                                         | quantise(c[2], 31, n));
        }
    }
    free(xt);
    free(rows);
    return true;
}
