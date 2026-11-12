#include "lyrics.h"
#include <stddef.h>

int lyrics_current_line(const float *times, int n, float elapsed_s)
{
    if (!times || n <= 0) return -1;
    int lo = 0, hi = n;                 /* first index whose time is > elapsed_s */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (times[mid] <= elapsed_s) lo = mid + 1;
        else hi = mid;
    }
    return lo - 1;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hex_to_rgb565(const char *hex, unsigned short *out, int max_px)
{
    int n = 0;
    while (*hex) {
        int d[4];
        for (int i = 0; i < 4; i++) {
            if (!hex[i]) return -1;
            d[i] = hex_digit(hex[i]);
            if (d[i] < 0) return -1;
        }
        if (n >= max_px) return -1;
        out[n++] = (unsigned short)((d[0] << 4) | d[1] | (d[2] << 12) | (d[3] << 8));
        hex += 4;
    }
    return n;
}
