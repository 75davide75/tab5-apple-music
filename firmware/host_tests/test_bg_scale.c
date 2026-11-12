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
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255, NULL, NULL));
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
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 229, NULL, NULL));
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
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255, NULL, NULL));
    int white = 1;
    for (int i = 0; i < DW * DH; i++) if (dst[i] != 0xFFFF) white = 0;
    CHECK(white);
    for (int i = 0; i < W * H; i++) src[i] = 0;
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255, NULL, NULL));
    int black = 1;
    for (int i = 0; i < DW * DH; i++) if (dst[i] != 0) black = 0;
    CHECK(black);
    free(dst);
}

static void test_bad_arguments_are_refused(void)
{
    uint16_t src[4] = {0}, dst[4] = {0x1234, 0x1234, 0x1234, 0x1234};
    CHECK(!bg_upscale(NULL, 2, 2, dst, 2, 2, 255, NULL, NULL));
    CHECK(!bg_upscale(src, 2, 2, NULL, 2, 2, 255, NULL, NULL));
    CHECK(!bg_upscale(src, 1, 2, dst, 2, 2, 255, NULL, NULL));
    CHECK(!bg_upscale(src, 2, 2, dst, 0, 2, 255, NULL, NULL));
    CHECK(dst[0] == 0x1234);
}

/* The cover's shadow is drawn into the background: full strength under the cover, fading
 * out smoothly with distance, and absent far away. */
static void test_shadow_fades_smoothly(void)
{
    uint16_t src[W * H];
    for (int i = 0; i < W * H; i++) src[i] = (uint16_t)((24 << 11) | (48 << 5) | 24);
    uint16_t *plain = calloc(DW * DH, 2), *dst = calloc(DW * DH, 2);
    bg_shadow_t sh = { .x = 40, .y = 40, .w = 80, .h = 80, .radius = 6, .blur = 40, .opa255 = 128 };
    CHECK(bg_upscale(src, W, H, plain, DW, DH, 255, NULL, NULL));
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255, &sh, NULL));
    const double level = 255.0 / 31.0, full = 24 * level;

    double inside = 0, far = 0;
    for (int y = 60; y < 100; y++) for (int x = 60; x < 100; x++) inside += r8(dst[y * DW + x]);
    inside /= 1600;
    CHECK(fabs(inside - full * (1 - 128 / 255.0)) < 1.0);
    for (int y = 0; y < DH; y++) for (int x = 220; x < DW; x++) far += r8(dst[y * DW + x]) - r8(plain[y * DW + x]);
    CHECK(far == 0);

    /* Walk right from the cover's edge through the blur, averaging over the rows beside the cover. */
    double prev = -1, worst = 0; int brightening = 1;
    for (int x = 120; x + 2 <= 170; x += 2) {
        double m = 0;
        for (int y = 50; y < 110; y++) for (int k = 0; k < 2; k++) m += r8(dst[y * DW + x + k]);
        m /= 120;
        if (prev >= 0) {
            if (m - prev > worst) worst = m - prev;
            if (m < prev - 0.2 * level) brightening = 0;
        }
        prev = m;
    }
    CHECK(brightening);
    CHECK(worst < 1.5 * level);          /* no hard rim: the whole drop is 12 levels */
    CHECK(fabs(prev - full) < 0.5);      /* gone by the end of the blur */
    free(plain); free(dst);
}

/* Right of flat.x0 the background is one single colour, so the lyrics panel can be an opaque
 * rectangle of that colour and scrolling never has to redraw the picture. The picture melts
 * into that colour over the `fade` pixels before it, and is untouched further left. */
static void test_flat_side_is_one_colour_with_soft_join(void)
{
    uint16_t src[W * H];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int v = 4 + x + y;
        src[y * W + x] = (uint16_t)((v << 11) | ((v * 2) << 5) | (31 - v));
    }
    uint16_t *plain = calloc(DW * DH, 2), *dst = calloc(DW * DH, 2);
    bg_flat_t flat = { .x0 = 200, .fade = 80, .colour = 0 };
    CHECK(bg_upscale(src, W, H, plain, DW, DH, 255, NULL, NULL));
    CHECK(bg_upscale(src, W, H, dst, DW, DH, 255, NULL, &flat));

    int one_colour = 1, untouched = 1;
    for (int y = 0; y < DH; y++) {
        for (int x = 200; x < DW; x++) if (dst[y * DW + x] != flat.colour) one_colour = 0;
        for (int x = 0; x < 120; x++) if (dst[y * DW + x] != plain[y * DW + x]) untouched = 0;
    }
    CHECK(one_colour);
    CHECK(untouched);
    /* the colour is the average of what it replaces, not something arbitrary */
    CHECK(fabs(r8(flat.colour) - column_mean(plain, 200, DW - 200, r8)) < 6.0);
    CHECK(fabs(b8(flat.colour) - column_mean(plain, 200, DW - 200, b8)) < 6.0);
    /* and the join has no visible step, on any row */
    const double level = 255.0 / 31.0;
    double worst = 0;
    for (int y = 0; y + 20 <= DH; y += 20) {
        for (int x = 120; x + 8 <= 208; x += 4) {
            double a = 0, b = 0;
            for (int j = 0; j < 20; j++) for (int k = 0; k < 4; k++) {
                a += r8(dst[(y + j) * DW + x + k]);
                b += r8(dst[(y + j) * DW + x + 4 + k]);
            }
            double d = fabs(a - b) / 80;
            if (d > worst) worst = d;
        }
    }
    CHECK(worst < 1.0 * level);
    free(plain); free(dst);
}

void run_bg_scale_tests(void)
{
    RUN(test_gradient_has_no_bands);
    RUN(test_flat_colour_is_preserved);
    RUN(test_extremes_do_not_overflow);
    RUN(test_bad_arguments_are_refused);
    RUN(test_shadow_fades_smoothly);
    RUN(test_flat_side_is_one_colour_with_soft_join);
}
