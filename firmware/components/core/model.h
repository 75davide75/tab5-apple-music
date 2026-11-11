#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { PB_STOPPED, PB_PAUSED, PB_PLAYING } pb_state_t;

typedef struct {
    char title[256], artist[256], album[256], player[64];
    bool title_truncated, artist_truncated;
    float duration_s;              /* <= 0: unknown */
    pb_state_t state;
    float rate;
    float elapsed_at_update_s;
    int64_t update_ms;
    int queue_index, queue_count;  /* -1: unknown; index is 0-based as sent by AMS */
    bool shuffle;
    uint32_t track_seq;            /* bumped whenever title, artist or album changes */
} now_playing_t;
