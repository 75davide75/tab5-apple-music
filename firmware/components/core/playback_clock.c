#include "playback_clock.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

float playback_elapsed_s(const now_playing_t *np, int64_t now_ms)
{
    float e = np->elapsed_at_update_s;
    if (np->state == PB_PLAYING) {
        e += (float)(now_ms - np->update_ms) / 1000.0f * np->rate;
    }
    if (e < 0) e = 0;
    if (np->duration_s > 0 && e > np->duration_s) e = np->duration_s;
    return e;
}

void format_mmss(float seconds, char out[8])
{
    if (isnan(seconds) || seconds < 0 || seconds >= 6000 * 60) {
        snprintf(out, 8, "--:--");
        return;
    }
    int total = (int)seconds;
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%d:%02d", total / 60, total % 60);
    memcpy(out, tmp, 7);
    out[7] = 0;
}
