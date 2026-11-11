#include "test_util.h"
#include "text_util.h"

static void test_url_encode_plain(void)
{
    char out[64];
    CHECK(url_encode("Faded-1_2.3~x", out, sizeof out));
    CHECK_STR(out, "Faded-1_2.3~x");
}

static void test_url_encode_reserved_and_utf8(void)
{
    char out[128];
    CHECK(url_encode("Suga Suga (feat. Frankie J) & più?", out, sizeof out));
    CHECK_STR(out, "Suga%20Suga%20%28feat.%20Frankie%20J%29%20%26%20pi%C3%B9%3F");
}

static void test_url_encode_empty(void)
{
    char out[4] = "zz";
    CHECK(url_encode("", out, sizeof out));
    CHECK_STR(out, "");
}

static void test_url_encode_overflow(void)
{
    char out[8] = "zzzzzzz";
    CHECK(!url_encode("a b c d", out, sizeof out));   /* needs 15 bytes + NUL */
    CHECK_STR(out, "");
    CHECK(url_encode("abcdefg", out, sizeof out));    /* exactly fits */
    CHECK(!url_encode("abcdefgh", out, sizeof out));
}

static void test_played_set(void)
{
    static played_set_t s;
    played_set_clear(&s);
    CHECK(!played_set_has(&s, 7));
    played_set_add(&s, 7);
    played_set_add(&s, 7);
    played_set_add(&s, 9);
    CHECK(s.n == 2);
    CHECK(played_set_has(&s, 7) && played_set_has(&s, 9) && !played_set_has(&s, 8));
    played_set_clear(&s);
    CHECK(!played_set_has(&s, 7));
}

static void test_played_set_full_is_safe(void)
{
    static played_set_t s;
    played_set_clear(&s);
    for (uint32_t i = 0; i < PLAYED_SET_MAX + 50; i++) played_set_add(&s, i + 1);
    CHECK(s.n == PLAYED_SET_MAX);
    CHECK(played_set_has(&s, 1));
    CHECK(!played_set_has(&s, PLAYED_SET_MAX + 10));
}

void run_text_util_tests(void)
{
    RUN(test_url_encode_plain);
    RUN(test_url_encode_reserved_and_utf8);
    RUN(test_url_encode_empty);
    RUN(test_url_encode_overflow);
    RUN(test_played_set);
    RUN(test_played_set_full_is_safe);
}
