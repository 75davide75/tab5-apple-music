#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "model.h"

/* Client for the Mac companion (found over mDNS as _tab5music._tcp).
 * Requests are queued and served by one worker task; callbacks run on that task. */

typedef struct {
    uint32_t id;
    const char *title, *artist;
    float duration_s;
} companion_track_t;

typedef struct {
    bool has_playlist;
    char name[128];
    int count;          /* tracks in the playlist */
    int current;        /* position of the current track in the playlist */
    bool ordered;       /* tracks[] is what really plays next */
    bool has_track_id;
    uint32_t track_id;  /* library id of the current track */
    int n;
    companion_track_t *tracks;
    void *json;         /* owns the strings */
} companion_list_t;

void companion_list_free(companion_list_t *list);
void companion_free(void *p);   /* for pixel buffers handed to callbacks */

typedef struct {
    /* list is NULL when the companion could not be reached. The callee frees it. */
    void (*on_list)(uint32_t seq, companion_list_t *list);
    /* cover: 400x400 RGB565, bg: 160x90 RGB565; either may be NULL. The callee frees both. */
    void (*on_cover)(uint32_t seq, uint8_t *cover, uint8_t *bg);
    /* px: 52x52 RGB565 or NULL. The callee frees it. */
    void (*on_thumb)(uint32_t track_id, uint8_t *px);
} companion_callbacks_t;

/* Connects to Wi-Fi and starts looking for the companion. esp_wifi_init() must already have run. */
esp_err_t companion_start(const companion_callbacks_t *cb);
bool companion_available(void);

void companion_request_list(const now_playing_t *np, uint32_t seq);
void companion_request_cover(bool has_track_id, uint32_t track_id, const now_playing_t *np, uint32_t seq);
void companion_request_thumb(uint32_t track_id);
