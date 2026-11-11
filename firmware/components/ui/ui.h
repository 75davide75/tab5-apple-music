#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "model.h"
#include "source_arbiter.h"

/* All functions must be called with the LVGL lock held (bsp_display_lock). */

#define UI_COVER_PX 400
#define UI_THUMB_PX 52
#define UI_BG_W 160
#define UI_BG_H 90
#define UI_MAX_ROWS 600

typedef enum { UI_CMD_TOGGLE, UI_CMD_NEXT, UI_CMD_PREV, UI_CMD_SEEK } ui_cmd_t;
typedef void (*ui_cmd_cb_t)(ui_cmd_t cmd, float seek_s);

void ui_init(ui_cmd_cb_t on_cmd);
void ui_set_now_playing(const now_playing_t *np, source_t src);
void ui_tick(float elapsed_s, float duration_s);

/* Pixel data is copied; NULL shows the neutral placeholder / plain dark background. */
void ui_set_cover(const uint8_t *rgb565, int w, int h);
void ui_set_background(const uint8_t *rgb565, int w, int h);

typedef struct {
    uint32_t id;
    const char *title, *artist;
    float duration_s;
    bool played, current;
} ui_row_t;

/* n == 0 shows the header only. Calling again with the same ids only refreshes the row states. */
void ui_set_list(const char *header, const char *subheader, bool shuffle_badge,
                 const ui_row_t *rows, int n);
void ui_set_thumb(uint32_t row_id, const uint8_t *rgb565);   /* UI_THUMB_PX square, copied */

/* Called (with the LVGL lock held) for each visible row that has no thumbnail yet. */
typedef void (*ui_thumb_req_cb_t)(uint32_t row_id);
void ui_on_thumb_needed(ui_thumb_req_cb_t cb);
