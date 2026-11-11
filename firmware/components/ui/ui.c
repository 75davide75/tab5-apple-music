#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "playback_clock.h"
#include "ui_theme.h"

static const char *TAG = "ui";

extern const uint8_t inter_regular_start[] asm("_binary_Inter_Regular_ttf_start");
extern const uint8_t inter_regular_end[] asm("_binary_Inter_Regular_ttf_end");
extern const uint8_t inter_semibold_start[] asm("_binary_Inter_SemiBold_ttf_start");
extern const uint8_t inter_semibold_end[] asm("_binary_Inter_SemiBold_ttf_end");
extern const lv_image_dsc_t icon_play, icon_pause, icon_next, icon_prev;

#define COL_BG lv_color_hex(0x1c1c1e)
#define COL_PLACEHOLDER lv_color_hex(0x3a3a3c)
#define WHITE lv_color_white()

typedef struct {
    uint32_t id;
    lv_obj_t *obj, *thumb, *title, *artist, *dur;
    uint8_t *thumb_buf;
    lv_image_dsc_t thumb_dsc;
    bool thumb_requested;
} row_t;

static lv_font_t *f_reg17, *f_reg20, *f_reg24, *f_semi24, *f_semi28;
static lv_obj_t *s_bg, *s_cover_box, *s_cover, *s_title, *s_artist, *s_slider, *s_time_l, *s_time_r;
static lv_obj_t *s_btn_toggle_img, *s_hdr, *s_sub, *s_pill, *s_list;
static lv_image_dsc_t s_bg_dsc, s_cover_dsc;
static uint8_t *s_bg_buf, *s_cover_buf;
static row_t *s_rows;
static int s_nrows;
static ui_cmd_cb_t s_on_cmd;
static ui_thumb_req_cb_t s_on_thumb;
static bool s_seekable;
static float s_duration;

static void *psram_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) ESP_LOGE(TAG, "out of memory (%u bytes)", (unsigned)n);
    return p;
}

static void rgb565_dsc(lv_image_dsc_t *d, const uint8_t *data, int w, int h)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = w * 2;
    d->data_size = (uint32_t)(w * h * 2);
    d->data = data;
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_opa_t opa, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, WHITE, 0);
    lv_obj_set_style_text_opa(l, opa, 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, lv_font_get_line_height(font));
    }
    lv_label_set_text(l, "");
    return l;
}

/* ---- transport buttons ---- */

static void btn_event(lv_event_t *e)
{
    ui_cmd_t cmd = (ui_cmd_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_on_cmd) s_on_cmd(cmd, 0);
}

static lv_obj_t *button(lv_obj_t *parent, const lv_image_dsc_t *icon, ui_cmd_t cmd, int cx, int cy)
{
    lv_obj_t *b = plain_obj(parent);
    lv_obj_set_size(b, UI_BTN, UI_BTN);
    lv_obj_set_pos(b, cx - UI_BTN / 2, cy - UI_BTN / 2);
    lv_obj_set_clickable(b, true);
    lv_obj_set_ext_click_area(b, 12);
    lv_obj_add_event_cb(b, btn_event, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
    lv_obj_t *img = lv_image_create(b);
    lv_image_set_src(img, icon);
    lv_obj_set_style_image_recolor(img, WHITE, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    lv_obj_set_style_opa(b, LV_OPA_60, LV_STATE_PRESSED);
    return img;
}

/* ---- progress bar ---- */

static void slider_event(lv_event_t *e)
{
    if (!s_seekable || s_duration <= 0 || !s_on_cmd) return;
    float pos = (float)lv_slider_get_value(s_slider) / 1000.0f * s_duration;
    s_on_cmd(UI_CMD_SEEK, pos);
}

/* ---- list ---- */

static void request_visible_thumbs(void)
{
    if (!s_on_thumb || s_nrows == 0) return;
    int top = lv_obj_get_scroll_y(s_list);
    int h = lv_obj_get_height(s_list);
    for (int i = 0; i < s_nrows; i++) {
        int y = i * UI_ROW_H;
        if (y + UI_ROW_H < top || y > top + h) continue;
        if (s_rows[i].thumb_buf || s_rows[i].thumb_requested) continue;
        s_rows[i].thumb_requested = true;
        s_on_thumb(s_rows[i].id);
    }
}

static void list_scroll_event(lv_event_t *e)
{
    request_visible_thumbs();
}

static void style_row(row_t *r, const ui_row_t *src)
{
    lv_obj_set_style_bg_opa(r->obj, src->current ? UI_OPA_ROW_NOW : LV_OPA_TRANSP, 0);
    bool dim = src->played && !src->current;
    lv_obj_set_style_text_opa(r->title, dim ? UI_OPA_ROW_PLAYED : LV_OPA_COVER, 0);
    lv_obj_set_style_text_opa(r->artist, dim ? 60 : 148, 0);
    lv_obj_set_style_text_opa(r->dur, dim ? 51 : 128, 0);
    lv_obj_set_style_opa(r->thumb, dim ? UI_OPA_ROW_PLAYED : LV_OPA_COVER, 0);
}

static void clear_rows(void)
{
    for (int i = 0; i < s_nrows; i++) heap_caps_free(s_rows[i].thumb_buf);
    lv_obj_clean(s_list);
    s_nrows = 0;
}

static void build_row(row_t *r, const ui_row_t *src, int index)
{
    memset(r, 0, sizeof *r);
    r->id = src->id;
    r->obj = plain_obj(s_list);
    lv_obj_set_size(r->obj, UI_RIGHT_W + 20, UI_ROW_H);
    lv_obj_set_pos(r->obj, 0, index * UI_ROW_H);
    lv_obj_set_style_radius(r->obj, 10, 0);
    lv_obj_set_style_bg_color(r->obj, WHITE, 0);

    r->thumb = lv_image_create(r->obj);
    lv_obj_set_size(r->thumb, UI_THUMB_PX, UI_THUMB_PX);
    lv_obj_set_pos(r->thumb, 10, (UI_ROW_H - UI_THUMB_PX) / 2);
    lv_obj_set_style_radius(r->thumb, 6, 0);
    lv_obj_set_style_clip_corner(r->thumb, true, 0);
    lv_obj_set_style_bg_color(r->thumb, COL_PLACEHOLDER, 0);
    lv_obj_set_style_bg_opa(r->thumb, LV_OPA_COVER, 0);

    int text_x = 10 + UI_THUMB_PX + 16;
    int text_w = UI_RIGHT_W + 20 - text_x - 80;
    r->title = label(r->obj, f_reg20, LV_OPA_COVER, text_x, 9, text_w);
    r->artist = label(r->obj, f_reg17, 148, text_x, 36, text_w);
    r->dur = label(r->obj, f_reg17, 128, 0, 0, 0);
    lv_obj_align(r->dur, LV_ALIGN_RIGHT_MID, -14, 0);

    lv_label_set_text(r->title, src->title ? src->title : "");
    lv_label_set_text(r->artist, src->artist ? src->artist : "");
    if (src->duration_s > 0) {
        char t[8];
        format_mmss(src->duration_s, t);
        lv_label_set_text(r->dur, t);
    }
    style_row(r, src);
}

void ui_set_list(const char *header, const char *subheader, bool shuffle_badge,
                 const ui_row_t *rows, int n)
{
    lv_label_set_text(s_hdr, header ? header : "");
    lv_label_set_text(s_sub, subheader ? subheader : "");
    if (shuffle_badge) lv_obj_set_hidden(s_pill, false);
    else lv_obj_set_hidden(s_pill, true);

    if (n > UI_MAX_ROWS) n = UI_MAX_ROWS;
    if (!rows) n = 0;

    bool same = (n == s_nrows);
    for (int i = 0; same && i < n; i++) same = (rows[i].id == s_rows[i].id);

    int current = -1;
    if (same) {
        for (int i = 0; i < n; i++) {
            style_row(&s_rows[i], &rows[i]);
            if (rows[i].current) current = i;
        }
    } else {
        clear_rows();
        for (int i = 0; i < n; i++) {
            build_row(&s_rows[i], &rows[i], i);
            if (rows[i].current) current = i;
        }
        s_nrows = n;
        lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    }
    if (current >= 0) {
        /* keep the current track one row below the top edge */
        int y = (current - 1) * UI_ROW_H;
        if (y < 0) y = 0;
        lv_obj_scroll_to_y(s_list, y, same ? LV_ANIM_ON : LV_ANIM_OFF);
    }
    request_visible_thumbs();
}

void ui_set_thumb(uint32_t row_id, const uint8_t *rgb565)
{
    for (int i = 0; i < s_nrows; i++) {
        row_t *r = &s_rows[i];
        if (r->id != row_id) continue;
        if (!rgb565) return;
        if (!r->thumb_buf) r->thumb_buf = psram_alloc(UI_THUMB_PX * UI_THUMB_PX * 2);
        if (!r->thumb_buf) return;
        memcpy(r->thumb_buf, rgb565, UI_THUMB_PX * UI_THUMB_PX * 2);
        rgb565_dsc(&r->thumb_dsc, r->thumb_buf, UI_THUMB_PX, UI_THUMB_PX);
        lv_image_set_src(r->thumb, &r->thumb_dsc);
        lv_obj_invalidate(r->thumb);
        return;
    }
}

void ui_on_thumb_needed(ui_thumb_req_cb_t cb)
{
    s_on_thumb = cb;
}

/* ---- cover and background ---- */

void ui_set_cover(const uint8_t *rgb565, int w, int h)
{
    if (!rgb565 || w != UI_COVER_PX || h != UI_COVER_PX || !s_cover_buf) {
        lv_obj_set_hidden(s_cover, true);
        return;
    }
    memcpy(s_cover_buf, rgb565, UI_COVER_PX * UI_COVER_PX * 2);
    rgb565_dsc(&s_cover_dsc, s_cover_buf, UI_COVER_PX, UI_COVER_PX);
    lv_image_set_src(s_cover, &s_cover_dsc);
    lv_obj_set_hidden(s_cover, false);
    lv_obj_invalidate(s_cover);
}

/* Bilinear upscale of the small blurred image to the full screen, with the dark veil baked in. */
void ui_set_background(const uint8_t *rgb565, int w, int h)
{
    if (!rgb565 || w < 2 || h < 2 || !s_bg_buf) {
        lv_obj_set_hidden(s_bg, true);
        return;
    }
    const uint16_t *src = (const uint16_t *)rgb565;
    uint16_t *dst = (uint16_t *)s_bg_buf;
    const int keep = 255 - UI_OPA_VEIL;
    for (int y = 0; y < UI_H; y++) {
        int fy = y * (h - 1) * 256 / (UI_H - 1);
        int y0 = fy >> 8, wy = fy & 255;
        int y1 = y0 + 1 < h ? y0 + 1 : y0;
        for (int x = 0; x < UI_W; x++) {
            int fx = x * (w - 1) * 256 / (UI_W - 1);
            int x0 = fx >> 8, wx = fx & 255;
            int x1 = x0 + 1 < w ? x0 + 1 : x0;
            uint16_t p[4] = {src[y0 * w + x0], src[y0 * w + x1], src[y1 * w + x0], src[y1 * w + x1]};
            int c[3];
            for (int k = 0; k < 3; k++) {
                int sh = k == 0 ? 11 : (k == 1 ? 5 : 0);
                int mask = k == 1 ? 0x3F : 0x1F;
                int a = (p[0] >> sh) & mask, b = (p[1] >> sh) & mask;
                int cc = (p[2] >> sh) & mask, d = (p[3] >> sh) & mask;
                int top = a * (256 - wx) + b * wx;
                int bot = cc * (256 - wx) + d * wx;
                c[k] = ((top * (256 - wy) + bot * wy) >> 16) * keep / 255;
            }
            dst[y * UI_W + x] = (uint16_t)((c[0] << 11) | (c[1] << 5) | c[2]);
        }
    }
    rgb565_dsc(&s_bg_dsc, s_bg_buf, UI_W, UI_H);
    lv_image_set_src(s_bg, &s_bg_dsc);
    lv_obj_set_hidden(s_bg, false);
    lv_obj_invalidate(s_bg);
}

/* ---- now playing ---- */

static void set_text_if_changed(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

void ui_set_now_playing(const now_playing_t *np, source_t src)
{
    char buf[300];
    snprintf(buf, sizeof buf, "%s%s", np->title, np->title_truncated ? "…" : "");
    set_text_if_changed(s_title, buf);
    snprintf(buf, sizeof buf, "%s%s", np->artist, np->artist_truncated ? "…" : "");
    set_text_if_changed(s_artist, buf);

    lv_image_set_src(s_btn_toggle_img, np->state == PB_PLAYING ? &icon_pause : &icon_play);

    s_seekable = (src == SRC_MAC);
    if (s_seekable) lv_obj_set_clickable(s_slider, true);
    else lv_obj_set_clickable(s_slider, false);
}

void ui_tick(float elapsed_s, float duration_s)
{
    char l[8], r[10];
    s_duration = duration_s;
    if (duration_s > 0) {
        if (elapsed_s > duration_s) elapsed_s = duration_s;
        if (!lv_slider_is_dragged(s_slider)) {
            lv_slider_set_value(s_slider, (int32_t)(elapsed_s / duration_s * 1000.0f), LV_ANIM_OFF);
        }
        char rem[8];
        format_mmss(elapsed_s, l);
        format_mmss(duration_s - elapsed_s, rem);
        snprintf(r, sizeof r, "-%s", rem);
    } else {
        lv_slider_set_value(s_slider, 0, LV_ANIM_OFF);
        snprintf(l, sizeof l, "--:--");
        snprintf(r, sizeof r, "--:--");
    }
    set_text_if_changed(s_time_l, l);
    set_text_if_changed(s_time_r, r);
}

/* ---- construction ---- */

void ui_init(ui_cmd_cb_t on_cmd)
{
    s_on_cmd = on_cmd;

    size_t reg_n = (size_t)(inter_regular_end - inter_regular_start);
    size_t semi_n = (size_t)(inter_semibold_end - inter_semibold_start);
    f_reg17 = lv_tiny_ttf_create_data(inter_regular_start, reg_n, 17);
    f_reg20 = lv_tiny_ttf_create_data(inter_regular_start, reg_n, 20);
    f_reg24 = lv_tiny_ttf_create_data(inter_regular_start, reg_n, 24);
    f_semi24 = lv_tiny_ttf_create_data(inter_semibold_start, semi_n, 24);
    f_semi28 = lv_tiny_ttf_create_data(inter_semibold_start, semi_n, 28);

    s_bg_buf = psram_alloc(UI_W * UI_H * 2);
    s_cover_buf = psram_alloc(UI_COVER_PX * UI_COVER_PX * 2);
    s_rows = psram_alloc(sizeof(row_t) * UI_MAX_ROWS);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_bg = lv_image_create(scr);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_obj_set_hidden(s_bg, true);

    /* left column */
    s_cover_box = plain_obj(scr);
    lv_obj_set_size(s_cover_box, UI_COVER_PX, UI_COVER_PX);
    lv_obj_set_pos(s_cover_box, UI_MARGIN_X, UI_MARGIN_Y);
    lv_obj_set_style_radius(s_cover_box, UI_COVER_RADIUS, 0);
    lv_obj_set_style_clip_corner(s_cover_box, true, 0);
    lv_obj_set_style_bg_color(s_cover_box, COL_PLACEHOLDER, 0);
    lv_obj_set_style_bg_opa(s_cover_box, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_cover_box, 50, 0);
    lv_obj_set_style_shadow_offset_y(s_cover_box, 18, 0);
    lv_obj_set_style_shadow_color(s_cover_box, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_cover_box, LV_OPA_50, 0);
    s_cover = lv_image_create(s_cover_box);
    lv_obj_set_hidden(s_cover, true);

    int y = UI_MARGIN_Y + UI_COVER_PX + 18;
    s_title = label(scr, f_semi28, LV_OPA_COVER, UI_MARGIN_X, y, UI_LEFT_W);
    s_artist = label(scr, f_reg24, UI_OPA_ARTIST, UI_MARGIN_X, y + 36, UI_LEFT_W);

    y += 36 + 44;
    s_slider = lv_slider_create(scr);
    lv_obj_remove_style_all(s_slider);
    lv_obj_set_size(s_slider, UI_LEFT_W, UI_BAR_H);
    lv_obj_set_pos(s_slider, UI_MARGIN_X, y);
    lv_slider_set_range(s_slider, 0, 1000);
    lv_obj_set_style_radius(s_slider, UI_BAR_H, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider, UI_OPA_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider, UI_BAR_H, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider, UI_OPA_BAR_FG, LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(s_slider, 22);
    lv_obj_set_clickable(s_slider, false);
    lv_obj_add_event_cb(s_slider, slider_event, LV_EVENT_RELEASED, NULL);

    y += UI_BAR_H + 8;
    s_time_l = label(scr, f_reg17, UI_OPA_TIME, UI_MARGIN_X, y, 0);
    s_time_r = label(scr, f_reg17, UI_OPA_TIME, 0, 0, 0);
    lv_obj_align(s_time_r, LV_ALIGN_TOP_RIGHT, -(UI_W - UI_MARGIN_X - UI_LEFT_W), y);
    lv_label_set_text(s_time_l, "--:--");
    lv_label_set_text(s_time_r, "--:--");

    int cy = (y + 22 + UI_H - UI_MARGIN_Y) / 2 + 6;
    int cx = UI_MARGIN_X + UI_LEFT_W / 2;
    button(scr, &icon_prev, UI_CMD_PREV, cx - 132, cy);
    s_btn_toggle_img = button(scr, &icon_play, UI_CMD_TOGGLE, cx, cy);
    button(scr, &icon_next, UI_CMD_NEXT, cx + 132, cy);

    /* right column */
    s_hdr = label(scr, f_semi24, LV_OPA_COVER, UI_RIGHT_X, UI_MARGIN_Y - 4, UI_RIGHT_W - 130);
    s_sub = label(scr, f_reg17, UI_OPA_SUB, UI_RIGHT_X, UI_MARGIN_Y + 28, UI_RIGHT_W - 130);

    s_pill = lv_label_create(scr);
    lv_label_set_text(s_pill, "Casuale");
    lv_obj_set_style_text_font(s_pill, f_reg17, 0);
    lv_obj_set_style_text_color(s_pill, WHITE, 0);
    lv_obj_set_style_bg_color(s_pill, WHITE, 0);
    lv_obj_set_style_bg_opa(s_pill, UI_OPA_PILL, 0);
    lv_obj_set_style_radius(s_pill, 20, 0);
    lv_obj_set_style_pad_hor(s_pill, 14, 0);
    lv_obj_set_style_pad_ver(s_pill, 6, 0);
    lv_obj_align(s_pill, LV_ALIGN_TOP_RIGHT, -UI_MARGIN_X, UI_MARGIN_Y + 4);
    lv_obj_set_hidden(s_pill, true);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, UI_RIGHT_X - 10, UI_MARGIN_Y + 66);
    lv_obj_set_size(s_list, UI_RIGHT_W + 20, UI_H - (UI_MARGIN_Y + 66));
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_list, list_scroll_event, LV_EVENT_SCROLL_END, NULL);
}
