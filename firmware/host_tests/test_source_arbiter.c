#include "test_util.h"
#include "source_arbiter.h"

static void test_arbiter_none_at_start(void)
{
    source_arbiter_t a; arbiter_init(&a);
    CHECK(arbiter_active(&a) == SRC_NONE);
}

static void test_arbiter_last_to_start_playing_wins(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 1);
    CHECK(arbiter_active(&a) == SRC_IOS_0);
    arbiter_report(&a, SRC_MAC, PB_PLAYING, 2);
    CHECK(arbiter_active(&a) == SRC_MAC);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 3);    /* repeated report is not a new start */
    CHECK(arbiter_active(&a) == SRC_MAC);
}

static void test_arbiter_falls_back_to_other_playing_source(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 1);
    arbiter_report(&a, SRC_MAC, PB_PLAYING, 2);
    arbiter_report(&a, SRC_MAC, PB_PAUSED, 3);
    CHECK(arbiter_active(&a) == SRC_IOS_0);
}

static void test_arbiter_paused_everywhere_keeps_last_active(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 1);
    arbiter_report(&a, SRC_MAC, PB_PLAYING, 2);
    arbiter_report(&a, SRC_IOS_0, PB_PAUSED, 3);
    arbiter_report(&a, SRC_MAC, PB_PAUSED, 4);
    CHECK(arbiter_active(&a) == SRC_MAC);
}

static void test_arbiter_paused_source_never_becomes_active_from_none(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PAUSED, 1);
    CHECK(arbiter_active(&a) == SRC_NONE);
}

static void test_arbiter_disconnect_of_active_source(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 1);
    arbiter_disconnected(&a, SRC_IOS_0);
    CHECK(arbiter_active(&a) == SRC_NONE);

    arbiter_report(&a, SRC_MAC, PB_PLAYING, 2);
    arbiter_report(&a, SRC_IOS_0, PB_PLAYING, 3);
    arbiter_disconnected(&a, SRC_IOS_0);
    CHECK(arbiter_active(&a) == SRC_MAC);
}

static void test_arbiter_two_ios_devices(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_IOS_0, PB_PAUSED, 1);
    arbiter_report(&a, SRC_IOS_1, PB_PLAYING, 2);
    CHECK(arbiter_active(&a) == SRC_IOS_1);
}

static void test_arbiter_ignores_invalid_source(void)
{
    source_arbiter_t a; arbiter_init(&a);
    arbiter_report(&a, SRC_NONE, PB_PLAYING, 1);
    arbiter_report(&a, (source_t)7, PB_PLAYING, 1);
    CHECK(arbiter_active(&a) == SRC_NONE);
}

void run_source_arbiter_tests(void)
{
    RUN(test_arbiter_none_at_start);
    RUN(test_arbiter_last_to_start_playing_wins);
    RUN(test_arbiter_falls_back_to_other_playing_source);
    RUN(test_arbiter_paused_everywhere_keeps_last_active);
    RUN(test_arbiter_paused_source_never_becomes_active_from_none);
    RUN(test_arbiter_disconnect_of_active_source);
    RUN(test_arbiter_two_ios_devices);
    RUN(test_arbiter_ignores_invalid_source);
}
