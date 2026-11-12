#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "model.h"
#include "source_arbiter.h"

/* All functions must be called with the LVGL lock held (bsp_display_lock). */

#define UI_COVER_PX 400

typedef enum { UI_CMD_TOGGLE, UI_CMD_NEXT, UI_CMD_PREV, UI_CMD_SEEK } ui_cmd_t;
typedef void (*ui_cmd_cb_t)(ui_cmd_t cmd, float seek_s);

void ui_init(ui_cmd_cb_t on_cmd);
void ui_set_now_playing(const now_playing_t *np, source_t src);
void ui_tick(float elapsed_s, float duration_s);

/* Pixel data is copied; NULL shows the neutral placeholder / plain dark background.
 * The background may be any small size: it is scaled up smoothly to the whole screen. */
void ui_set_cover(const uint8_t *rgb565, int w, int h);
void ui_set_background(const uint8_t *rgb565, int w, int h);

/* Lyrics for the right-hand side. The text is copied.
 *  - n > 0: lyrics layout (cover and controls on the left, lyrics on the right)
 *  - n == 0: no lyrics for this track: large cover on the left, controls on the right */
void ui_set_lyrics(const char *const *lines, int n, bool synced);
/* Empties the lyrics while a new track is being looked up, without changing the layout. */
void ui_clear_lyrics(void);
/* Highlights and scrolls to a line of synced lyrics; -1 before the first line. */
void ui_set_lyric_line(int index);
