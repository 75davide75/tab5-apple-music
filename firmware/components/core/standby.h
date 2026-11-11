#pragma once
#include "model.h"

#define STANDBY_PAUSE_TIMEOUT_MS 180000
#define STANDBY_TOUCH_TIMEOUT_MS 30000

typedef struct standby {
    pb_state_t state;
    int64_t lit_until_ms;   /* screen stays on until this time when not playing */
} standby_t;

void standby_init(standby_t *s, int64_t now_ms);
void standby_on_playback(standby_t *s, pb_state_t state, int64_t now_ms);
void standby_on_touch(standby_t *s, int64_t now_ms);
bool standby_screen_on(const standby_t *s, int64_t now_ms);
