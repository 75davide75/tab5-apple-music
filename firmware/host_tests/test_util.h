#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>

extern int g_failures, g_checks;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { g_checks++; if (strcmp((a), (b)) != 0) { g_failures++; \
    printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define CHECK_NEAR(a, b) CHECK(fabs((double)(a) - (double)(b)) < 0.01)
#define RUN(fn) do { printf("%s\n", #fn); fn(); } while (0)
