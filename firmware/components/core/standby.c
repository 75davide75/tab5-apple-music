#include "standby.h"

void standby_init(standby_t *s, int64_t now_ms)
{
    s->state = PB_STOPPED;
    s->lit_until_ms = now_ms;
}

void standby_on_playback(standby_t *s, pb_state_t state, int64_t now_ms)
{
    if (state == PB_PAUSED && s->state == PB_PLAYING) {
        s->lit_until_ms = now_ms + STANDBY_PAUSE_TIMEOUT_MS;
    } else if (state == PB_STOPPED) {
        s->lit_until_ms = now_ms;
    }
    s->state = state;
}

void standby_on_touch(standby_t *s, int64_t now_ms)
{
    if (s->state == PB_PLAYING) return;
    int64_t until = now_ms + STANDBY_TOUCH_TIMEOUT_MS;
    if (until > s->lit_until_ms) s->lit_until_ms = until;
}

bool standby_screen_on(const standby_t *s, int64_t now_ms)
{
    return s->state == PB_PLAYING || now_ms < s->lit_until_ms;
}
