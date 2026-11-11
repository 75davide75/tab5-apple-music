#include "test_util.h"
#include "ams_parse.h"
#include "playback_clock.h"

static now_playing_t make(pb_state_t st, float elapsed, float duration)
{
    now_playing_t np; now_playing_init(&np);
    np.state = st; np.rate = st == PB_PLAYING ? 1.0f : 0.0f;
    np.elapsed_at_update_s = elapsed; np.update_ms = 1000; np.duration_s = duration;
    return np;
}

static void test_clock_advances_while_playing(void)
{
    now_playing_t np = make(PB_PLAYING, 10, 200);
    CHECK_NEAR(playback_elapsed_s(&np, 6000), 15.0);
}

static void test_clock_frozen_while_paused(void)
{
    now_playing_t np = make(PB_PAUSED, 10, 200);
    CHECK_NEAR(playback_elapsed_s(&np, 6000), 10.0);
}

static void test_clock_clamped_to_duration(void)
{
    now_playing_t np = make(PB_PLAYING, 10, 20);
    CHECK_NEAR(playback_elapsed_s(&np, 61000), 20.0);
}

static void test_clock_unknown_duration_keeps_growing(void)
{
    now_playing_t np = make(PB_PLAYING, 10, 0);
    CHECK_NEAR(playback_elapsed_s(&np, 61000), 70.0);
}

static void test_clock_never_negative(void)
{
    now_playing_t np = make(PB_PLAYING, 10, 200);
    np.rate = -2.0f;                       /* rewinding */
    CHECK_NEAR(playback_elapsed_s(&np, 61000), 0.0);
}

static void test_format_mmss(void)
{
    char b[8];
    format_mmss(84, b);    CHECK_STR(b, "1:24");
    format_mmss(0, b);     CHECK_STR(b, "0:00");
    format_mmss(3725, b);  CHECK_STR(b, "62:05");
    format_mmss(-1, b);    CHECK_STR(b, "--:--");
    format_mmss(NAN, b);   CHECK_STR(b, "--:--");
    format_mmss(1e9f, b);  CHECK(strlen(b) < 8);
}

void run_playback_clock_tests(void)
{
    RUN(test_clock_advances_while_playing);
    RUN(test_clock_frozen_while_paused);
    RUN(test_clock_clamped_to_duration);
    RUN(test_clock_unknown_duration_keeps_growing);
    RUN(test_clock_never_negative);
    RUN(test_format_mmss);
}
