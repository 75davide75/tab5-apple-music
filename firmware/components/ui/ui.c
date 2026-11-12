#include "esp_log.h"
#include "esp_timer.h"
#include "bg_scale.h"
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

static lv_font_t *f_reg17, *f_reg24, *f_semi28, *f_semi32;
static lv_obj_t *s_bg, *s_cover_box, *s_cover, *s_title, *s_artist, *s_slider, *s_time_l, *s_time_r;
static lv_obj_t *s_btn[3], *s_btn_toggle_img, *s_lyrics;
static lv_obj_t *s_lines[UI_MAX_LYRIC_LINES];
static lv_image_dsc_t s_bg_dsc, s_cover_dsc;
static uint8_t *s_bg_buf, *s_cover_buf;
#define BG_SRC_MAX (64 * 36)
static uint16_t s_bg_src[BG_SRC_MAX];       /* the small colour image last given, kept for redraws */
static int s_bg_src_w, s_bg_src_h;
/* The background is redrawn from ui_tick, never from ui_init: the main task that runs ui_init
 * has its stack in the chip's slow low-power RAM, where this redraw took 12 seconds. */
static bool s_bg_dirty;
static int s_nlines, s_cur_line = -1;
static bool s_synced, s_lyrics_layout;
static ui_cmd_cb_t s_on_cmd;
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

static lv_obj_t *button(lv_obj_t *parent, const lv_image_dsc_t *icon, ui_cmd_t cmd, lv_obj_t **container)
{
    lv_obj_t *b = plain_obj(parent);
    *container = b;
    lv_obj_set_size(b, UI_BTN, UI_BTN);
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

/* ---- background ---- */

/* The background is redrawn whole whenever its colours or the cover's place change, because
 * the cover's shadow is part of it: drawn there it is dithered and shows no bands. */
static void render_background(void)
{
    if (!s_bg_buf) return;
    static const uint16_t flat[4] = {0x18E3, 0x18E3, 0x18E3, 0x18E3};      /* COL_BG, when there is no image */
    bool has = s_bg_src_w > 0;
    int cover = s_lyrics_layout ? UI_L_COVER : UI_N_COVER;
    int cover_x = s_lyrics_layout ? UI_L_MARGIN_X : UI_N_COVER_X;
    int cover_y = s_lyrics_layout ? UI_L_MARGIN_Y : (UI_H - UI_N_COVER) / 2;
    /* Narrower than the cover and pushed down, so the shadow falls mostly below it. */
    bg_shadow_t shadow = { .x = cover_x + 20, .y = cover_y + 36, .w = cover - 40, .h = cover - 16,
                           .radius = UI_COVER_RADIUS, .blur = 90, .opa255 = 72 };
    int64_t began = esp_timer_get_time();
    bool ok = bg_upscale(has ? s_bg_src : flat, has ? s_bg_src_w : 2, has ? s_bg_src_h : 2,
                         (uint16_t *)s_bg_buf, UI_W, UI_H, has ? 255 - UI_OPA_VEIL : 255, &shadow);
    ESP_LOGI("ui", "background drawn in %d ms", (int)((esp_timer_get_time() - began) / 1000));
    if (!ok) {
        lv_obj_set_hidden(s_bg, true);
        return;
    }
    rgb565_dsc(&s_bg_dsc, s_bg_buf, UI_W, UI_H);
    lv_image_set_src(s_bg, &s_bg_dsc);
    lv_obj_set_hidden(s_bg, false);
    lv_obj_invalidate(s_bg);
}

/* ---- layout ---- */

/* Places cover, texts, bar and buttons for one of the two layouts. */
static void apply_layout(bool lyrics)
{
    s_lyrics_layout = lyrics;
    int cover = lyrics ? UI_L_COVER : UI_N_COVER;
    int cover_x = lyrics ? UI_L_MARGIN_X : UI_N_COVER_X;
    int cover_y = lyrics ? UI_L_MARGIN_Y : (UI_H - UI_N_COVER) / 2;
    int col_x = lyrics ? UI_L_MARGIN_X : UI_N_PANEL_X;      /* column holding texts and controls */
    int col_w = lyrics ? UI_L_COVER : UI_N_PANEL_W;
    int y = lyrics ? UI_L_MARGIN_Y + UI_L_COVER + 18 : 200;

    lv_obj_set_pos(s_cover_box, cover_x, cover_y);
    lv_obj_set_size(s_cover_box, cover, cover);
    lv_obj_set_size(s_cover, cover, cover);

    const lv_font_t *title_font = lyrics ? f_semi28 : f_semi32;
    lv_obj_set_style_text_font(s_title, title_font, 0);
    lv_obj_set_pos(s_title, col_x, y);
    lv_obj_set_size(s_title, col_w, lv_font_get_line_height(title_font));
    y += lv_font_get_line_height(title_font) + 4;
    lv_obj_set_pos(s_artist, col_x, y);
    lv_obj_set_width(s_artist, col_w);

    y += lyrics ? 44 : 70;
    lv_obj_set_pos(s_slider, col_x, y);
    lv_obj_set_width(s_slider, col_w);
    y += UI_BAR_H + 8;
    lv_obj_set_pos(s_time_l, col_x, y);
    lv_obj_align(s_time_r, LV_ALIGN_TOP_RIGHT, -(UI_W - col_x - col_w), y);

    int cy = lyrics ? (y + 22 + UI_H - UI_L_MARGIN_Y) / 2 + 6 : y + 110;
    int cx = col_x + col_w / 2;
    int step = lyrics ? 132 : 160;
    for (int i = 0; i < 3; i++) {
        lv_obj_set_pos(s_btn[i], cx + (i - 1) * step - UI_BTN / 2, cy - UI_BTN / 2);
    }
    lv_obj_set_hidden(s_lyrics, !lyrics);
    s_bg_dirty = true;          /* the cover moved, and its shadow with it */
}

/* ---- lyrics ---- */

void ui_clear_lyrics(void)
{
    lv_obj_clean(s_lyrics);
    s_nlines = 0;
    s_cur_line = -1;
    lv_obj_scroll_to_y(s_lyrics, 0, LV_ANIM_OFF);
}

void ui_set_lyrics(const char *const *lines, int n, bool synced)
{
    ui_clear_lyrics();
    if (!lines || n <= 0) {
        if (s_lyrics_layout) apply_layout(false);
        return;
    }
    if (n > UI_MAX_LYRIC_LINES) n = UI_MAX_LYRIC_LINES;
    s_synced = synced;
    for (int i = 0; i < n; i++) {
        lv_obj_t *l = lv_label_create(s_lyrics);
        lv_obj_set_width(l, UI_L_LYRICS_W);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_font(l, f_semi32, 0);
        lv_obj_set_style_text_color(l, WHITE, 0);
        lv_obj_set_style_text_opa(l, synced ? UI_OPA_LYRIC_DIM : UI_OPA_LYRIC_PLAIN, 0);
        lv_label_set_text(l, lines[i] ? lines[i] : "");
        s_lines[i] = l;
    }
    s_nlines = n;
    if (!s_lyrics_layout) apply_layout(true);
    lv_obj_update_layout(s_lyrics);
}

/* Moving the lyrics redraws most of the screen, which this tablet manages about ten times a
 * second (measured: 96 ms per frame). A gliding scroll at that rate looks jerky, so the text
 * steps to the new line in a single frame instead. Set to LV_ANIM_ON to glide again. */
#define LYRIC_SCROLL_ANIM LV_ANIM_OFF

void ui_set_lyric_line(int index)
{
    if (!s_synced || index == s_cur_line || index >= s_nlines) return;
    if (s_cur_line >= 0 && s_cur_line < s_nlines) {
        lv_obj_set_style_text_opa(s_lines[s_cur_line], UI_OPA_LYRIC_DIM, 0);
    }
    s_cur_line = index;
    if (index < 0) {
        lv_obj_scroll_to_y(s_lyrics, 0, LYRIC_SCROLL_ANIM);
        return;
    }
    lv_obj_set_style_text_opa(s_lines[index], LV_OPA_COVER, 0);

    /* Bring the line being sung to a third of the way down. Working from where the line
     * is on screen right now stays correct even if an earlier scroll is still animating. */
    lv_area_t line, box;
    lv_obj_update_layout(s_lyrics);
    lv_obj_get_coords(s_lines[index], &line);
    lv_obj_get_coords(s_lyrics, &box);
    int32_t wanted = box.y1 + lv_obj_get_height(s_lyrics) / 3;
    lv_obj_scroll_by_bounded(s_lyrics, 0, wanted - line.y1, LYRIC_SCROLL_ANIM);
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
    lv_obj_fade_in(s_cover, 300, 0);
}

/* Fills the screen with the small blurred image, smoothed and with the dark veil baked in. */
void ui_set_background(const uint8_t *rgb565, int w, int h)
{
    s_bg_src_w = 0;
    if (rgb565 && w >= 2 && h >= 2 && w * h <= BG_SRC_MAX) {
        memcpy(s_bg_src, rgb565, (size_t)(w * h) * 2);
        s_bg_src_w = w;
        s_bg_src_h = h;
    }
    s_bg_dirty = true;
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
    if (s_bg_dirty) {
        s_bg_dirty = false;
        render_background();
    }
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
    f_reg24 = lv_tiny_ttf_create_data(inter_regular_start, reg_n, 24);
    f_semi28 = lv_tiny_ttf_create_data(inter_semibold_start, semi_n, 28);
    f_semi32 = lv_tiny_ttf_create_data(inter_semibold_start, semi_n, 32);

    s_bg_buf = psram_alloc(UI_W * UI_H * 2);
    s_cover_buf = psram_alloc(UI_COVER_PX * UI_COVER_PX * 2);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_bg = lv_image_create(scr);
    lv_obj_set_pos(s_bg, 0, 0);
    lv_obj_set_hidden(s_bg, true);

    s_cover_box = plain_obj(scr);
    lv_obj_set_style_radius(s_cover_box, UI_COVER_RADIUS, 0);
    lv_obj_set_style_clip_corner(s_cover_box, true, 0);
    lv_obj_set_style_bg_color(s_cover_box, COL_PLACEHOLDER, 0);
    lv_obj_set_style_bg_opa(s_cover_box, LV_OPA_COVER, 0);
    s_cover = lv_image_create(s_cover_box);
    lv_image_set_inner_align(s_cover, LV_IMAGE_ALIGN_STRETCH);   /* the large layout scales the 400 px cover up */
    lv_obj_set_hidden(s_cover, true);

    s_title = label(scr, f_semi28, LV_OPA_COVER, 0, 0, 100);
    s_artist = label(scr, f_reg24, UI_OPA_ARTIST, 0, 0, 100);

    s_slider = lv_slider_create(scr);
    lv_obj_remove_style_all(s_slider);
    lv_obj_set_height(s_slider, UI_BAR_H);
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

    s_time_l = label(scr, f_reg17, UI_OPA_TIME, 0, 0, 0);
    s_time_r = label(scr, f_reg17, UI_OPA_TIME, 0, 0, 0);
    lv_label_set_text(s_time_l, "--:--");
    lv_label_set_text(s_time_r, "--:--");

    button(scr, &icon_prev, UI_CMD_PREV, &s_btn[0]);
    s_btn_toggle_img = button(scr, &icon_play, UI_CMD_TOGGLE, &s_btn[1]);
    button(scr, &icon_next, UI_CMD_NEXT, &s_btn[2]);

    /* Lyrics scroll inside this column; the padding lets the first and last lines reach the reading position. */
    s_lyrics = lv_obj_create(scr);
    lv_obj_remove_style_all(s_lyrics);
    lv_obj_set_pos(s_lyrics, UI_L_LYRICS_X, 0);
    lv_obj_set_size(s_lyrics, UI_L_LYRICS_W, UI_H);
    lv_obj_set_flex_flow(s_lyrics, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_lyrics, UI_LYRIC_GAP, 0);
    lv_obj_set_style_pad_top(s_lyrics, UI_H / 3, 0);
    lv_obj_set_style_pad_bottom(s_lyrics, UI_H / 2, 0);
    lv_obj_set_scroll_dir(s_lyrics, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_lyrics, LV_SCROLLBAR_MODE_OFF);

    apply_layout(false);
}
