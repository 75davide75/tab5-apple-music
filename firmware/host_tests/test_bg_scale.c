#include <stdlib.h>
#include "bg_scale.h"
#include "test_util.h"

enum { W = 16, H = 9, DW = 320, DH = 180 };

static double r8(uint16_t p) { return ((p >> 11) & 31) * 255.0 / 31.0; }
static double g8(uint16_t p) { return ((p >> 5) & 63) * 255.0 / 63.0; }
static double b8(uint16_t p) { return (p & 31) * 255.0 / 31.0; }

static double column_mean(const uint16_t *img, int x0, int n, double (*ch)(uint16_t))
{
    double s = 0;
    for (int y = 0; y < DH; y++) for (int x = x0; x < x0 + n; x++) s += ch(img[y * DW + x]);
    return s / (n * DH);
}

/* A slow ramp must not turn into flat bands with visible jumps between them. */
static void test_gradient_has_no_bands(void)
{
    uint16_t src[W * H];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int v = 6 + x;                                  /* one 5-bit shade per source pixel */
        src[y * W + x] = (uint16_t)((v << 11) | ((v * 2) << 5) | v);
    }
    uint16_t *dst = calloc(DW * DH, 2);
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255));
    const double level = 255.0 / 31.0;
    double prev = column_mean(dst, 0, 4, r8), worst = 0;
    int rising = 1;
    for (int x = 4; x + 4 <= DW; x += 4) {
        double m = column_mean(dst, x, 4, r8);
        if (m - prev > worst) worst = m - prev;
        if (m < prev - 0.15 * level) rising = 0;
        prev = m;
    }
    CHECK(worst < 0.5 * level);
    CHECK(rising);
    free(dst);
}

/* A flat colour keeps its brightness on average, also after darkening. */
static void test_flat_colour_is_preserved(void)
{
    uint16_t src[W * H];
    for (int i = 0; i < W * H; i++) src[i] = (uint16_t)((13 << 11) | (40 << 5) | 21);
    uint16_t *dst = calloc(DW * DH, 2);
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 229));
    double k = 229.0 / 255.0;
    CHECK(fabs(column_mean(dst, 0, DW, r8) - 13 * 255.0 / 31.0 * k) < 0.7);
    CHECK(fabs(column_mean(dst, 0, DW, g8) - 40 * 255.0 / 63.0 * k) < 0.7);
    CHECK(fabs(column_mean(dst, 0, DW, b8) - 21 * 255.0 / 31.0 * k) < 0.7);
    free(dst);
}

static void test_extremes_do_not_overflow(void)
{
    uint16_t src[W * H];
    for (int i = 0; i < W * H; i++) src[i] = 0xFFFF;
    uint16_t *dst = calloc(DW * DH, 2);
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255));
    int white = 1;
    for (int i = 0; i < DW * DH; i++) if (dst[i] != 0xFFFF) white = 0;
    CHECK(white);
    for (int i = 0; i < W * H; i++) src[i] = 0;
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255));
    int black = 1;
    for (int i = 0; i < DW * DH; i++) if (dst[i] != 0) black = 0;
    CHECK(black);
    free(dst);
}

static void test_bad_arguments_are_refused(void)
{
    uint16_t src[4] = {0}, dst[4] = {0x1234, 0x1234, 0x1234, 0x1234};
    CHECK(!bg_upscale(NULL, 2, 2, dst, 2, 2, 255));
    CHECK(!bg_upscale(src, 2, 2, NULL, 2, 2, 255));
    CHECK(!bg_upscale(src, 1, 2, dst, 2, 2, 255));
    CHECK(!bg_upscale(src, 2, 2, dst, 0, 2, 255));
    CHECK(dst[0] == 0x1234);
}

void run_bg_scale_tests(void)
{
    RUN(test_gradient_has_no_bands);
    RUN(test_flat_colour_is_preserved);
    RUN(test_extremes_do_not_overflow);
    RUN(test_bad_arguments_are_refused);
}
