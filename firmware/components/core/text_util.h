#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Percent-encodes src for use as a URL query value. Returns false (and an empty
 * string) if the result does not fit in cap bytes including the terminator. */
bool url_encode(const char *src, char *dst, size_t cap);

/* Tracks already heard in the current playlist session. */
#define PLAYED_SET_MAX 600
typedef struct {
    uint32_t ids[PLAYED_SET_MAX];
    int n;
} played_set_t;

void played_set_clear(played_set_t *s);
void played_set_add(played_set_t *s, uint32_t id);
bool played_set_has(const played_set_t *s, uint32_t id);
