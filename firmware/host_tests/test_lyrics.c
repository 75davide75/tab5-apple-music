#include "test_util.h"
#include "lyrics.h"

static const float T[] = {11.0f, 15.5f, 15.5f, 20.0f, 31.2f};

static void test_lyrics_before_first_line(void)
{
    CHECK(lyrics_current_line(T, 5, 0.0f) == -1);
    CHECK(lyrics_current_line(T, 5, 10.99f) == -1);
}

static void test_lyrics_exact_and_between(void)
{
    CHECK(lyrics_current_line(T, 5, 11.0f) == 0);
    CHECK(lyrics_current_line(T, 5, 13.0f) == 0);
    CHECK(lyrics_current_line(T, 5, 20.0f) == 3);
    CHECK(lyrics_current_line(T, 5, 25.0f) == 3);
}

static void test_lyrics_equal_times_pick_the_last(void)
{
    CHECK(lyrics_current_line(T, 5, 15.5f) == 2);
}

static void test_lyrics_after_last_line(void)
{
    CHECK(lyrics_current_line(T, 5, 999.0f) == 4);
}

static void test_lyrics_empty(void)
{
    CHECK(lyrics_current_line(T, 0, 5.0f) == -1);
    CHECK(lyrics_current_line(NULL, 0, 5.0f) == -1);
}

static void test_hex_to_rgb565(void)
{
    unsigned short px[4] = {0};
    CHECK(hex_to_rgb565("00f8E007", px, 4) == 2);
    CHECK(px[0] == 0xF800 && px[1] == 0x07E0);
    CHECK(hex_to_rgb565("", px, 4) == 0);
}

static void test_hex_to_rgb565_rejects_bad_input(void)
{
    unsigned short px[2];
    CHECK(hex_to_rgb565("00f8e0", px, 2) == -1);        /* not a whole pixel */
    CHECK(hex_to_rgb565("00g8e007", px, 2) == -1);      /* not hex */
    CHECK(hex_to_rgb565("00f8e00700f8", px, 2) == -1);  /* more pixels than room */
}

void run_lyrics_tests(void)
{
    RUN(test_lyrics_before_first_line);
    RUN(test_lyrics_exact_and_between);
    RUN(test_lyrics_equal_times_pick_the_last);
    RUN(test_lyrics_after_last_line);
    RUN(test_lyrics_empty);
    RUN(test_hex_to_rgb565);
    RUN(test_hex_to_rgb565_rejects_bad_input);
}
