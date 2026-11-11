#include "test_util.h"
#include "standby.h"

static void test_standby_off_at_boot(void)
{
    standby_t s; standby_init(&s, 0);
    CHECK(!standby_screen_on(&s, 0));
    CHECK(!standby_screen_on(&s, 1000000));
}

static void test_standby_on_while_playing(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 100);
    CHECK(standby_screen_on(&s, 100));
    CHECK(standby_screen_on(&s, 100 + 10 * 3600 * 1000));
}

static void test_standby_pause_timeout(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_playback(&s, PB_PAUSED, 1000);
    CHECK(standby_screen_on(&s, 1000 + 179999));
    CHECK(!standby_screen_on(&s, 1000 + 180000));
}

static void test_standby_repeated_pause_reports_do_not_extend(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_playback(&s, PB_PAUSED, 1000);
    standby_on_playback(&s, PB_PAUSED, 100000);
    CHECK(!standby_screen_on(&s, 1000 + 180000));
}

static void test_standby_resume_clears_timer(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_playback(&s, PB_PAUSED, 1000);
    standby_on_playback(&s, PB_PLAYING, 50000);
    CHECK(standby_screen_on(&s, 1000 + 180000 + 5));
    standby_on_playback(&s, PB_PAUSED, 400000);
    CHECK(standby_screen_on(&s, 400000 + 179999));
    CHECK(!standby_screen_on(&s, 400000 + 180000));
}

static void test_standby_stop_turns_off_immediately(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_playback(&s, PB_STOPPED, 500);
    CHECK(!standby_screen_on(&s, 500));
}

static void test_standby_paused_from_off_stays_off(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PAUSED, 100);
    CHECK(!standby_screen_on(&s, 100));
}

static void test_standby_touch_wakes_for_30s(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_touch(&s, 1000);
    CHECK(standby_screen_on(&s, 1000));
    CHECK(standby_screen_on(&s, 1000 + 29999));
    CHECK(!standby_screen_on(&s, 1000 + 30000));
}

static void test_standby_touch_while_playing_adds_no_deadline(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_touch(&s, 1000);
    CHECK(standby_screen_on(&s, 1000 + 30000 + 1));
    standby_on_playback(&s, PB_PAUSED, 100000);
    CHECK(standby_screen_on(&s, 100000 + 179999));
    CHECK(!standby_screen_on(&s, 100000 + 180000));
}

static void test_standby_touch_while_paused_keeps_lit(void)
{
    standby_t s; standby_init(&s, 0);
    standby_on_playback(&s, PB_PLAYING, 0);
    standby_on_playback(&s, PB_PAUSED, 0);
    standby_on_touch(&s, 170000);
    CHECK(standby_screen_on(&s, 199999));
    CHECK(!standby_screen_on(&s, 200000));
    standby_on_touch(&s, 1000);              /* early touch never shortens the pause window */
}

void run_standby_tests(void)
{
    RUN(test_standby_off_at_boot);
    RUN(test_standby_on_while_playing);
    RUN(test_standby_pause_timeout);
    RUN(test_standby_repeated_pause_reports_do_not_extend);
    RUN(test_standby_resume_clears_timer);
    RUN(test_standby_stop_turns_off_immediately);
    RUN(test_standby_paused_from_off_stays_off);
    RUN(test_standby_touch_wakes_for_30s);
    RUN(test_standby_touch_while_playing_adds_no_deadline);
    RUN(test_standby_touch_while_paused_keeps_lit);
}
