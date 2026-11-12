#include "mac_state.h"
#include <string.h>

/* Copies src (NULL = empty) into dst without splitting a UTF-8 character. */
static bool set_field(char *dst, size_t cap, const char *src, bool *cut)
{
    if (!src) src = "";
    size_t n = strlen(src);
    bool was_cut = false;
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
        was_cut = true;
    }
    if (cut) *cut = was_cut;
    if (strlen(dst) == n && memcmp(dst, src, n) == 0) return false;
    memcpy(dst, src, n);
    dst[n] = 0;
    return true;
}

bool mac_state_apply(now_playing_t *np, const char *state, const char *title, const char *artist,
                     const char *album, float duration_s, float elapsed_s, int64_t now_ms)
{
    pb_state_t st = PB_STOPPED;
    if (state && strcmp(state, "playing") == 0) st = PB_PLAYING;
    else if (state && strcmp(state, "paused") == 0) st = PB_PAUSED;

    bool track_changed = false;
    track_changed |= set_field(np->title, sizeof np->title, title, &np->title_truncated);
    track_changed |= set_field(np->artist, sizeof np->artist, artist, &np->artist_truncated);
    track_changed |= set_field(np->album, sizeof np->album, album, NULL);
    if (track_changed) np->track_seq++;

    bool changed = track_changed || st != np->state || duration_s != np->duration_s ||
                   elapsed_s != np->elapsed_at_update_s;
    np->state = st;
    np->rate = st == PB_PLAYING ? 1.0f : 0.0f;
    np->duration_s = duration_s;
    np->elapsed_at_update_s = elapsed_s;
    np->update_ms = now_ms;
    np->queue_index = -1;
    np->queue_count = -1;
    return changed;
}
