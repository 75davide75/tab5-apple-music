#pragma once
#include "model.h"

/* Applies one poll of the Mac's player state to np. state is "playing", "paused" or
 * anything else (stopped). track_seq is bumped only when title, artist or album change.
 * Returns true if anything the screen shows changed. */
bool mac_state_apply(now_playing_t *np, const char *state, const char *title, const char *artist,
                     const char *album, float duration_s, float elapsed_s, int64_t now_ms);
