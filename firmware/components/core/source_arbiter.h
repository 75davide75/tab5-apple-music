#pragma once
#include "model.h"

typedef enum { SRC_NONE = -1, SRC_IOS_0 = 0, SRC_IOS_1 = 1, SRC_MAC = 2, SRC_COUNT = 3 } source_t;

typedef struct source_arbiter {
    pb_state_t state[SRC_COUNT];
    uint32_t play_order[SRC_COUNT];   /* higher = started playing more recently */
    uint32_t counter;
    source_t active;
} source_arbiter_t;

void arbiter_init(source_arbiter_t *a);
void arbiter_report(source_arbiter_t *a, source_t src, pb_state_t state, int64_t now_ms);
void arbiter_disconnected(source_arbiter_t *a, source_t src);   /* same as PB_STOPPED */
source_t arbiter_active(const source_arbiter_t *a);
