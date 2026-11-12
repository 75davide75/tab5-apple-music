#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "model.h"

/* Client for the Mac companion (found over mDNS as _tab5music._tcp).
 * Requests are queued and served by one worker task; callbacks run on that task. */

#define COMPANION_COVER_PX 400
#define COMPANION_BG_W 16
#define COMPANION_BG_H 9

typedef struct {
    bool has_lyrics;
    bool synced;            /* times[] are valid */
    int n;
    const char **lines;
    float *times;           /* ascending, seconds from the start of the track */
    bool has_bg;
    uint16_t bg[COMPANION_BG_W * COMPANION_BG_H];   /* RGB565 */
    void *json;             /* owns the strings */
} companion_track_t;

void companion_track_free(companion_track_t *t);
void companion_free(void *p);   /* for pixel buffers handed to callbacks */

typedef struct {
    /* info is NULL when the companion could not be reached. The callee frees it. */
    void (*on_track)(uint32_t seq, companion_track_t *info);
    /* cover: COMPANION_COVER_PX square RGB565, or NULL. The callee frees it. */
    void (*on_cover)(uint32_t seq, uint8_t *cover);
} companion_callbacks_t;

/* Connects to Wi-Fi and starts looking for the companion. esp_wifi_init() must already have run. */
esp_err_t companion_start(const companion_callbacks_t *cb);
bool companion_available(void);

/* Asks for the lyrics and background of a track, then for its cover. */
void companion_request_track(const now_playing_t *np, uint32_t seq);
