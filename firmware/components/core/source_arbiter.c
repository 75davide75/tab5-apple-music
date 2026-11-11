#include "source_arbiter.h"
#include <string.h>

void arbiter_init(source_arbiter_t *a)
{
    memset(a, 0, sizeof *a);
    for (int i = 0; i < SRC_COUNT; i++) a->state[i] = PB_STOPPED;
    a->active = SRC_NONE;
}

static void recompute(source_arbiter_t *a)
{
    source_t best = SRC_NONE;
    for (int i = 0; i < SRC_COUNT; i++) {
        if (a->state[i] == PB_PLAYING && (best == SRC_NONE || a->play_order[i] > a->play_order[best])) {
            best = (source_t)i;
        }
    }
    if (best != SRC_NONE) {
        a->active = best;
    } else if (a->active != SRC_NONE && a->state[a->active] != PB_PAUSED) {
        a->active = SRC_NONE;
    }
}

void arbiter_report(source_arbiter_t *a, source_t src, pb_state_t state, int64_t now_ms)
{
    (void)now_ms;
    if (src < 0 || src >= SRC_COUNT) return;
    if (state == PB_PLAYING && a->state[src] != PB_PLAYING) a->play_order[src] = ++a->counter;
    a->state[src] = state;
    recompute(a);
}

void arbiter_disconnected(source_arbiter_t *a, source_t src)
{
    arbiter_report(a, src, PB_STOPPED, 0);
}

source_t arbiter_active(const source_arbiter_t *a)
{
    return a->active;
}
