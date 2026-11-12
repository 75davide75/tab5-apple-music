#include "test_util.h"
#include "ams_parse.h"

static bool apply(now_playing_t *np, uint8_t e, uint8_t a, uint8_t flags, const char *v, int64_t now)
{
    return ams_apply_update(np, e, a, flags, v, strlen(v), now);
}

static void test_playback_info(void)
{
    now_playing_t np; now_playing_init(&np);
    CHECK(apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_PLAYBACK_INFO, 0, "1,1.0,83.5", 5000));
    CHECK(np.state == PB_PLAYING);
    CHECK_NEAR(np.rate, 1.0);
    CHECK_NEAR(np.elapsed_at_update_s, 83.5);
    CHECK(np.update_ms == 5000);
}

static void test_playback_info_paused(void)
{
    now_playing_t np; now_playing_init(&np);
    apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_PLAYBACK_INFO, 0, "0,0.0,12", 1);
    CHECK(np.state == PB_PAUSED);
    CHECK_NEAR(np.elapsed_at_update_s, 12.0);
}

static void test_playback_info_malformed(void)
{
    const char *bad[] = {"", "1", "x,y,z"};
    for (int i = 0; i < 3; i++) {
        now_playing_t np; now_playing_init(&np);
        apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_PLAYBACK_INFO, 0, "1,1.0,83.5", 5000);
        CHECK(!apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_PLAYBACK_INFO, 0, bad[i], 9000));
        CHECK(np.state == PB_PLAYING);
        CHECK_NEAR(np.elapsed_at_update_s, 83.5);
        CHECK(np.update_ms == 5000);
    }
}

static void test_title_utf8_commas(void)
{
    now_playing_t np; now_playing_init(&np);
    const char *t = "Città, vuota 🎧";
    CHECK(apply(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, 0, t, 0));
    CHECK_STR(np.title, t);
    CHECK(!np.title_truncated);
}

static void test_title_truncated_flag(void)
{
    now_playing_t np; now_playing_init(&np);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, AMS_FLAG_TRUNCATED, "Un titolo lung", 0);
    CHECK(np.title_truncated);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, 0, "Corto", 0);
    CHECK(!np.title_truncated);
}

static void test_title_overlong(void)
{
    now_playing_t np; now_playing_init(&np);
    char big[401];
    for (int i = 0; i < 200; i++) { big[2 * i] = (char)0xC3; big[2 * i + 1] = (char)0xA8; } /* "è" x200 */
    big[400] = 0;
    ams_apply_update(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, 0, big, 400, 0);
    size_t n = strlen(np.title);
    CHECK(n == 254);                          /* 255 would split a 2-byte character */
    CHECK((unsigned char)np.title[n - 2] == 0xC3 && (unsigned char)np.title[n - 1] == 0xA8);
    CHECK(np.title_truncated);                /* we cut it, so it must be shown as cut */
}

static void test_value_not_nul_terminated(void)
{
    now_playing_t np; now_playing_init(&np);
    ams_apply_update(&np, AMS_ENT_TRACK, AMS_TRACK_ARTIST, 0, "Alan WalkerXXXX", 11, 0);
    CHECK_STR(np.artist, "Alan Walker");
}

static void test_track_seq_increments(void)
{
    now_playing_t np; now_playing_init(&np);
    uint32_t s0 = np.track_seq;
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, 0, "Faded", 0);
    CHECK(np.track_seq == s0 + 1);
    CHECK(!apply(&np, AMS_ENT_TRACK, AMS_TRACK_TITLE, 0, "Faded", 0));
    CHECK(np.track_seq == s0 + 1);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_ARTIST, 0, "Alan Walker", 0);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_ALBUM, 0, "Faded - EP", 0);
    CHECK(np.track_seq == s0 + 3);
}

static void test_queue(void)
{
    now_playing_t np; now_playing_init(&np);
    CHECK(np.queue_index == -1 && np.queue_count == -1);
    apply(&np, AMS_ENT_QUEUE, AMS_QUEUE_INDEX, 0, "13", 0);
    apply(&np, AMS_ENT_QUEUE, AMS_QUEUE_COUNT, 0, "42", 0);
    apply(&np, AMS_ENT_QUEUE, AMS_QUEUE_SHUFFLE, 0, "1", 0);
    CHECK(np.queue_index == 13);
    CHECK(np.queue_count == 42);
    CHECK(np.shuffle);
    apply(&np, AMS_ENT_QUEUE, AMS_QUEUE_SHUFFLE, 0, "0", 0);
    CHECK(!np.shuffle);
    apply(&np, AMS_ENT_QUEUE, AMS_QUEUE_INDEX, 0, "", 0);
    CHECK(np.queue_index == -1);
}

static void test_duration(void)
{
    now_playing_t np; now_playing_init(&np);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_DURATION, 0, "212.627", 0);
    CHECK_NEAR(np.duration_s, 212.627);
    apply(&np, AMS_ENT_TRACK, AMS_TRACK_DURATION, 0, "", 0);
    CHECK(np.duration_s <= 0);
}

static void test_player_is_music(void)
{
    now_playing_t np; now_playing_init(&np);
    CHECK(!ams_player_is_music(&np));
    apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_NAME, 0, "Musica", 0);
    CHECK(ams_player_is_music(&np));
    apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_NAME, 0, "Music", 0);
    CHECK(ams_player_is_music(&np));
    apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_NAME, 0, "YouTube", 0);
    CHECK(!ams_player_is_music(&np));
}

/* The app's name arrives in the iPhone's language. */
static void test_player_is_music_other_languages(void)
{
    static const char *const yes[] = { "Musik", "Musique", "M\xc3\xbasica", "Muziek", "Muzyka",
                                       "\xd0\x9c\xd1\x83\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb0",
                                       "\xe3\x83\x9f\xe3\x83\xa5\xe3\x83\xbc\xe3\x82\xb8\xe3\x83\x83\xe3\x82\xaf",
                                       "\xe9\x9f\xb3\xe4\xb9\x90", "Apple Music" };
    static const char *const no[] = { "YouTube Music", "Amazon Music", "Musicals", "WhatsApp", "Safari",
                                      "Spotify", "Podcast", "TV", "" };
    now_playing_t np; now_playing_init(&np);
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) {
        apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_NAME, 0, yes[i], 0);
        CHECK(ams_player_is_music(&np));
    }
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) {
        apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_NAME, 0, no[i], 0);
        CHECK(!ams_player_is_music(&np));
    }
}

static void test_unknown_entity_ignored(void)
{
    now_playing_t np; now_playing_init(&np);
    CHECK(!apply(&np, 9, 0, 0, "x", 0));
    CHECK(!apply(&np, AMS_ENT_PLAYER, AMS_PLAYER_VOLUME, 0, "0.5", 0));
}

void run_ams_parse_tests(void)
{
    RUN(test_playback_info);
    RUN(test_playback_info_paused);
    RUN(test_playback_info_malformed);
    RUN(test_title_utf8_commas);
    RUN(test_title_truncated_flag);
    RUN(test_title_overlong);
    RUN(test_value_not_nul_terminated);
    RUN(test_track_seq_increments);
    RUN(test_queue);
    RUN(test_duration);
    RUN(test_player_is_music);
    RUN(test_player_is_music_other_languages);
    RUN(test_unknown_entity_ignored);
}
