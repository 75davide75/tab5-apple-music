#pragma once
#include <stddef.h>
#include "model.h"

enum { AMS_ENT_PLAYER = 0, AMS_ENT_QUEUE = 1, AMS_ENT_TRACK = 2 };
enum { AMS_PLAYER_NAME = 0, AMS_PLAYER_PLAYBACK_INFO = 1, AMS_PLAYER_VOLUME = 2 };
enum { AMS_QUEUE_INDEX = 0, AMS_QUEUE_COUNT = 1, AMS_QUEUE_SHUFFLE = 2, AMS_QUEUE_REPEAT = 3 };
enum { AMS_TRACK_ARTIST = 0, AMS_TRACK_ALBUM = 1, AMS_TRACK_TITLE = 2, AMS_TRACK_DURATION = 3 };
#define AMS_FLAG_TRUNCATED 0x01

void now_playing_init(now_playing_t *np);

/* Applies one Entity Update notification. Returns true if np changed. */
bool ams_apply_update(now_playing_t *np, uint8_t entity, uint8_t attr, uint8_t flags,
                      const char *value, size_t len, int64_t now_ms);

/* True when the reported player is Apple's Music app ("Music" / "Musica"). */
bool ams_player_is_music(const now_playing_t *np);
