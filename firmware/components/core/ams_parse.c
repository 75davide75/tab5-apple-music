#include "ams_parse.h"
#include <stdlib.h>
#include <string.h>

void now_playing_init(now_playing_t *np)
{
    memset(np, 0, sizeof *np);
    np->state = PB_STOPPED;
    np->queue_index = -1;
    np->queue_count = -1;
}

/* Copies up to cap-1 bytes without splitting a UTF-8 character.
 * Sets *cut when the value did not fit. Returns true if dst changed. */
static bool set_text(char *dst, size_t cap, const char *value, size_t len, bool *cut)
{
    size_t n = len;
    bool was_cut = false;
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)value[n] & 0xC0) == 0x80) n--;
        was_cut = true;
    }
    if (cut) *cut = was_cut;
    if (strlen(dst) == n && memcmp(dst, value, n) == 0) return false;
    memcpy(dst, value, n);
    dst[n] = 0;
    return true;
}

/* Parses a decimal number occupying the whole of [s, s+len). */
static bool parse_float(const char *s, size_t len, float *out)
{
    char buf[32];
    if (len == 0 || len >= sizeof buf) return false;
    memcpy(buf, s, len);
    buf[len] = 0;
    char *end;
    float v = strtof(buf, &end);
    if (end == buf || *end != 0) return false;
    *out = v;
    return true;
}

static bool read_playback_info(const char *value, size_t len, float *state, float *rate, float *elapsed)
{
    /* "state,rate,elapsed" */
    const char *c1 = memchr(value, ',', len);
    if (!c1) return false;
    const char *c2 = memchr(c1 + 1, ',', len - (size_t)(c1 + 1 - value));
    if (!c2) return false;
    return parse_float(value, (size_t)(c1 - value), state)
        && parse_float(c1 + 1, (size_t)(c2 - c1 - 1), rate)
        && parse_float(c2 + 1, len - (size_t)(c2 + 1 - value), elapsed);
}

static bool parse_playback_info(now_playing_t *np, const char *value, size_t len, int64_t now_ms)
{
    float state, rate, elapsed;
    if (!read_playback_info(value, len, &state, &rate, &elapsed)) {
        /* The iPhone sends an empty value when its player closes or its queue ends: nothing is
         * playing. Keeping the previous state here would leave the screen on for ever. */
        if (np->state == PB_STOPPED) return false;
        np->state = PB_STOPPED;
        np->rate = 0;
        np->update_ms = now_ms;
        return true;
    }

    /* 0 paused, 1 playing, 2 rewinding, 3 fast-forwarding */
    np->state = ((int)state == 0) ? PB_PAUSED : PB_PLAYING;
    np->rate = rate;
    np->elapsed_at_update_s = elapsed;
    np->update_ms = now_ms;
    return true;
}

static bool set_int(int *dst, const char *value, size_t len)
{
    float f;
    int v = parse_float(value, len, &f) ? (int)f : -1;
    if (*dst == v) return false;
    *dst = v;
    return true;
}

bool ams_apply_update(now_playing_t *np, uint8_t entity, uint8_t attr, uint8_t flags,
                      const char *value, size_t len, int64_t now_ms)
{
    bool truncated = (flags & AMS_FLAG_TRUNCATED) != 0;
    bool cut = false, changed = false;

    switch (entity) {
    case AMS_ENT_PLAYER:
        if (attr == AMS_PLAYER_NAME) return set_text(np->player, sizeof np->player, value, len, NULL);
        if (attr == AMS_PLAYER_PLAYBACK_INFO) return parse_playback_info(np, value, len, now_ms);
        return false;

    case AMS_ENT_QUEUE:
        if (attr == AMS_QUEUE_INDEX) return set_int(&np->queue_index, value, len);
        if (attr == AMS_QUEUE_COUNT) return set_int(&np->queue_count, value, len);
        if (attr == AMS_QUEUE_SHUFFLE) {
            float f;
            bool on = parse_float(value, len, &f) && (int)f != 0;
            if (on == np->shuffle) return false;
            np->shuffle = on;
            return true;
        }
        return false;

    case AMS_ENT_TRACK:
        switch (attr) {
        case AMS_TRACK_TITLE:
            changed = set_text(np->title, sizeof np->title, value, len, &cut);
            if (np->title_truncated != (truncated || cut)) {
                np->title_truncated = truncated || cut;
                if (!changed) return true;
            }
            break;
        case AMS_TRACK_ARTIST:
            changed = set_text(np->artist, sizeof np->artist, value, len, &cut);
            if (np->artist_truncated != (truncated || cut)) {
                np->artist_truncated = truncated || cut;
                if (!changed) return true;
            }
            break;
        case AMS_TRACK_ALBUM:
            changed = set_text(np->album, sizeof np->album, value, len, NULL);
            break;
        case AMS_TRACK_DURATION: {
            float d;
            if (!parse_float(value, len, &d)) d = 0;
            if (d == np->duration_s) return false;
            np->duration_s = d;
            return true;
        }
        default:
            return false;
        }
        if (changed) np->track_seq++;
        return changed;
    }
    return false;
}

/* AMS gives only the app's display name, in the iPhone's language. These are the names of
 * Apple's Music app; the match is exact, so "YouTube Music" and the like never count.
 * Only "Musica" (Italian) has been seen on a real device; the others are from Apple's
 * localisations and are untested. */
static const char *const MUSIC_APP_NAMES[] = {
    "Music", "Apple Music",
    "Musica",                       /* Italian */
    "Musik",                        /* German, Swedish, Danish, Indonesian */
    "Musique",                      /* French */
    "Música",                       /* Spanish, Portuguese, Catalan */
    "Muziek",                       /* Dutch */
    "Musikk",                       /* Norwegian */
    "Musiikki",                     /* Finnish */
    "Muzyka",                       /* Polish */
    "Hudba",                        /* Czech, Slovak */
    "Zene",                         /* Hungarian */
    "Muzică",                       /* Romanian */
    "Glazba",                       /* Croatian */
    "Müzik",                        /* Turkish */
    "Muzik",                        /* Malay */
    "Nhạc",                         /* Vietnamese */
    "Μουσική",                      /* Greek */
    "Музыка",                       /* Russian */
    "Музика",                       /* Ukrainian */
    "ミュージック",                 /* Japanese */
    "音乐", "音樂",                 /* Chinese */
    "음악",                         /* Korean */
    "เพลง",                         /* Thai */
    "מוזיקה",                       /* Hebrew */
    "الموسيقى",                     /* Arabic */
    "संगीत",                        /* Hindi */
};

bool ams_player_is_music(const now_playing_t *np)
{
    for (size_t i = 0; i < sizeof MUSIC_APP_NAMES / sizeof MUSIC_APP_NAMES[0]; i++) {
        if (strcmp(np->player, MUSIC_APP_NAMES[i]) == 0) return true;
    }
    return false;
}
