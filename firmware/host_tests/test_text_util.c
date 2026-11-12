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

void run_text_util_tests(void)
{
    RUN(test_url_encode_plain);
    RUN(test_url_encode_reserved_and_utf8);
    RUN(test_url_encode_empty);
    RUN(test_url_encode_overflow);
}
