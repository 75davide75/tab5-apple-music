#include "test_util.h"
#include "ams_parse.h"
#include "mac_state.h"

static void test_mac_state_playing(void)
{
    now_playing_t np; now_playing_init(&np);
    CHECK(mac_state_apply(&np, "playing", "Titolo", "Artista", "Album", 200.0f, 12.5f, 7000));
    CHECK(np.state == PB_PLAYING);
    CHECK_NEAR(np.rate, 1.0);
    CHECK_STR(np.title, "Titolo");
    CHECK_STR(np.artist, "Artista");
    CHECK_NEAR(np.duration_s, 200.0);
    CHECK_NEAR(np.elapsed_at_update_s, 12.5);
    CHECK(np.update_ms == 7000);
    CHECK(np.track_seq == 1);
}

static void test_mac_state_same_track_keeps_seq(void)
{
    now_playing_t np; now_playing_init(&np);
    mac_state_apply(&np, "playing", "Titolo", "Artista", "Album", 200.0f, 12.5f, 7000);
    mac_state_apply(&np, "playing", "Titolo", "Artista", "Album", 200.0f, 13.5f, 8000);
    CHECK(np.track_seq == 1);
    CHECK_NEAR(np.elapsed_at_update_s, 13.5);
    mac_state_apply(&np, "playing", "Altro", "Artista", "Album", 180.0f, 0.0f, 9000);
    CHECK(np.track_seq == 2);
}

static void test_mac_state_paused_and_stopped(void)
{
    now_playing_t np; now_playing_init(&np);
    mac_state_apply(&np, "playing", "Titolo", "Artista", "Album", 200.0f, 12.5f, 7000);
    CHECK(mac_state_apply(&np, "paused", "Titolo", "Artista", "Album", 200.0f, 12.5f, 8000));
    CHECK(np.state == PB_PAUSED);
    CHECK(mac_state_apply(&np, "stopped", "", "", "", 0.0f, 0.0f, 9000));
    CHECK(np.state == PB_STOPPED);
    CHECK(mac_state_apply(&np, "whatever", "", "", "", 0.0f, 0.0f, 9500) == false);
    CHECK(np.state == PB_STOPPED);
}

static void test_mac_state_tolerates_null_and_long_text(void)
{
    now_playing_t np; now_playing_init(&np);
    char big[600];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    mac_state_apply(&np, "playing", big, NULL, NULL, 10.0f, 1.0f, 1);
    CHECK(strlen(np.title) == 255);
    CHECK(np.title_truncated);
    CHECK_STR(np.artist, "");
    mac_state_apply(&np, NULL, NULL, NULL, NULL, 0.0f, 0.0f, 2);
    CHECK(np.state == PB_STOPPED);
}

void run_mac_state_tests(void)
{
    RUN(test_mac_state_playing);
    RUN(test_mac_state_same_track_keeps_seq);
    RUN(test_mac_state_paused_and_stopped);
    RUN(test_mac_state_tolerates_null_and_long_text);
}
