#pragma once
#include "model.h"

/* Elapsed seconds extrapolated from the last update; clamped to [0, duration] when duration is known. */
float playback_elapsed_s(const now_playing_t *np, int64_t now_ms);

/* "m:ss" (minutes not wrapped at 60); "--:--" for negative or NaN input. */
void format_mmss(float seconds, char out[8]);
