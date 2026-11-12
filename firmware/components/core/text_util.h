#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Percent-encodes src for use as a URL query value. Returns false (and an empty
 * string) if the result does not fit in cap bytes including the terminator. */
bool url_encode(const char *src, char *dst, size_t cap);
