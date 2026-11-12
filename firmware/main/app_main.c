#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "ams_ble.h"
#include "ams_parse.h"
#include "companion.h"
#include "playback_clock.h"
#include "source_arbiter.h"
#include "standby.h"
#include "text_util.h"
#include "ui.h"

#define BRIGHTNESS_ON 80
#define UI_PERIOD_MS 250

static const char *TAG = "app";

/* Shared state, guarded by s_mx. Never take the display lock while holding s_mx. */
static SemaphoreHandle_t s_mx;
static now_playing_t s_np[SRC_COUNT];
static source_arbiter_t s_arb;
static standby_t s_standby;
static bool s_dirty = true;

/* Playlist shown in the right column (guarded by s_mx). */
static companion_list_t *s_list;
static bool s_list_dirty;
static bool s_list_missing;         /* the last request got no answer from the Mac */
static uint32_t s_req_seq;          /* id of the latest companion request; older answers are dropped */
static now_playing_t s_req_np;      /* the track that request was about */
static played_set_t s_played;
static char s_playlist_name[128];
static bool s_have_cur_id;
static uint32_t s_cur_id;
static ui_row_t *s_rows;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* Only Apple's Music app counts as playback; anything else keeps the screen off. */
static void report_ios(int slot)
{
    pb_state_t st = ams_player_is_music(&s_np[slot]) ? s_np[slot].state : PB_STOPPED;
    arbiter_report(&s_arb, (source_t)slot, st, now_ms());
}

static void on_ams_update(uint8_t slot, uint8_t entity, uint8_t attr, uint8_t flags,
                          const char *value, size_t len)
{
    if (slot >= AMS_MAX_PEERS) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (ams_apply_update(&s_np[slot], entity, attr, flags, value, len, now_ms())) {
        report_ios(slot);
        s_dirty = true;
    }
    xSemaphoreGive(s_mx);
}

static void on_ams_conn(uint8_t slot, bool connected)
{
    if (slot >= AMS_MAX_PEERS) return;
    ESP_LOGI(TAG, "iOS slot %d %s", slot, connected ? "connected" : "disconnected");
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (!connected) {
        now_playing_init(&s_np[slot]);
        arbiter_disconnected(&s_arb, (source_t)slot);
        s_dirty = true;
    }
    xSemaphoreGive(s_mx);
}

/* Runs on the LVGL task. */
static void on_ui_cmd(ui_cmd_t cmd, float seek_s)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    source_t src = arbiter_active(&s_arb);
    xSemaphoreGive(s_mx);

    if (src == SRC_IOS_0 || src == SRC_IOS_1) {
        static const uint8_t map[] = {[UI_CMD_TOGGLE] = AMS_CMD_TOGGLE, [UI_CMD_NEXT] = AMS_CMD_NEXT,
                                      [UI_CMD_PREV] = AMS_CMD_PREV};
        if (cmd != UI_CMD_SEEK) ams_ble_send_command((uint8_t)src, map[cmd]);
    }
}

/* Runs on the LVGL task: a touch on the dark screen only wakes it. */
static void on_touch(lv_event_t *e)
{
    int64_t t = now_ms();
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool was_on = standby_screen_on(&s_standby, t);
    standby_on_touch(&s_standby, t);
    xSemaphoreGive(s_mx);
    if (!was_on) lv_indev_wait_release(lv_event_get_target(e));
}

/* ---- companion callbacks (worker task) ---- */

static void on_list(uint32_t seq, companion_list_t *list)
{
    now_playing_t np;
    bool has_id = false;
    uint32_t id = 0;

    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (seq != s_req_seq) {              /* the track changed while we were asking */
        xSemaphoreGive(s_mx);
        companion_list_free(list);
        return;
    }
    const char *name = (list && list->has_playlist) ? list->name : "";
    if (strcmp(name, s_playlist_name) != 0) {
        played_set_clear(&s_played);     /* a new playlist starts a new session */
        snprintf(s_playlist_name, sizeof s_playlist_name, "%s", name);
        s_have_cur_id = false;
    }
    has_id = list && list->has_track_id;
    id = has_id ? list->track_id : 0;
    if (s_have_cur_id && (!has_id || id != s_cur_id)) played_set_add(&s_played, s_cur_id);
    s_have_cur_id = has_id;
    s_cur_id = id;

    companion_list_t *old = s_list;
    s_list = list;
    s_list_dirty = true;
    s_list_missing = (list == NULL);
    np = s_req_np;
    xSemaphoreGive(s_mx);
    ESP_LOGI(TAG, "list for request %u: %s, %d tracks, track id %s", (unsigned)seq,
             list ? (list->has_playlist ? list->name : "no playlist") : "Mac unreachable", list ? list->n : 0,
             has_id ? "known" : "unknown");

    companion_list_free(old);
    companion_request_cover(has_id, id, &np, seq);
}

static void on_cover(uint32_t seq, uint8_t *cover, uint8_t *bg)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool current = (seq == s_req_seq);
    xSemaphoreGive(s_mx);
    ESP_LOGI(TAG, "cover for request %u: cover=%s background=%s%s", (unsigned)seq, cover ? "yes" : "no",
             bg ? "yes" : "no", current ? "" : " (stale, dropped)");
    if (current && bsp_display_lock(1000)) {
        ui_set_cover(cover, UI_COVER_PX, UI_COVER_PX);
        ui_set_background(bg, UI_BG_W, UI_BG_H);
        bsp_display_unlock();
    }
    companion_free(cover);
    companion_free(bg);
}

static void on_thumb(uint32_t track_id, uint8_t *px)
{
    if (px && bsp_display_lock(1000)) {
        ui_set_thumb(track_id, px);
        bsp_display_unlock();
    }
    companion_free(px);
}

/* Runs on the LVGL task. */
static void on_thumb_needed(uint32_t row_id)
{
    companion_request_thumb(row_id);
}

/* Fills the right column. Call with the display lock held. */
static void render_list(const now_playing_t *np)
{
    char pos[48] = "";
    if (np->queue_index >= 0 && np->queue_count > 0) {
        snprintf(pos, sizeof pos, "Brano %d di %d", np->queue_index + 1, np->queue_count);
    }

    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_list_dirty = false;
    if (!s_list || !s_list->has_playlist || !s_rows) {
        xSemaphoreGive(s_mx);
        ui_set_list(pos, "", np->shuffle, NULL, 0);
        return;
    }
    int n = s_list->n < UI_MAX_ROWS ? s_list->n : UI_MAX_ROWS;
    for (int i = 0; i < n; i++) {
        const companion_track_t *t = &s_list->tracks[i];
        bool current = !s_list->ordered && s_list->has_track_id && t->id == s_list->track_id;
        s_rows[i] = (ui_row_t){.id = t->id, .title = t->title, .artist = t->artist, .duration_s = t->duration_s,
                               .current = current,
                               .played = !s_list->ordered && played_set_has(&s_played, t->id)};
    }
    /* Only a queue we know to be in playlist order may be called "next". */
    if (s_list->ordered) ui_set_list("Prossimi brani", s_list->name, false, s_rows, n);
    else ui_set_list(s_list->name, pos, np->shuffle, s_rows, n);
    xSemaphoreGive(s_mx);
}

static void ui_task(void *arg)
{
    now_playing_t shown;
    now_playing_init(&shown);
    source_t shown_src = SRC_NONE;
    bool screen_on = false;

    /* what the last companion request was made for */
    uint32_t asked_seq = UINT32_MAX;
    source_t asked_src = SRC_NONE;
    bool asked_shuffle = false;
    int asked_count = -2;
    int64_t changed_ms = 0;
    bool ask_pending = false;
    int64_t retry_ms = 0;

    for (;;) {
        int64_t t = now_ms();
        xSemaphoreTake(s_mx, portMAX_DELAY);
        source_t src = arbiter_active(&s_arb);
        bool dirty = s_dirty || src != shown_src;
        s_dirty = false;
        if (src != SRC_NONE) shown = s_np[src];
        else if (shown.state == PB_PLAYING) shown.state = PB_PAUSED;   /* source went away */
        standby_on_playback(&s_standby, src == SRC_NONE ? PB_STOPPED : shown.state, t);
        bool on = standby_screen_on(&s_standby, t);
        bool list_dirty = s_list_dirty;
        bool list_missing = s_list_missing;
        xSemaphoreGive(s_mx);

        /* A track change arrives as several separate updates (artist, album, title):
         * wait until they settle before asking the Mac about it. */
        if (src != SRC_NONE && (shown.track_seq != asked_seq || src != asked_src ||
                                shown.shuffle != asked_shuffle || shown.queue_count != asked_count)) {
            asked_seq = shown.track_seq;
            asked_src = src;
            asked_shuffle = shown.shuffle;
            asked_count = shown.queue_count;
            changed_ms = t;
            ask_pending = true;
        }
        /* The Mac was unreachable last time: ask again once it is back. */
        if (list_missing && !ask_pending && src != SRC_NONE && companion_available() && t - retry_ms > 3000) {
            retry_ms = t;
            ask_pending = true;
        }
        if (ask_pending && t - changed_ms >= 400 && shown.title[0]) {
            ask_pending = false;
            xSemaphoreTake(s_mx, portMAX_DELAY);
            uint32_t seq = ++s_req_seq;
            s_req_np = shown;
            xSemaphoreGive(s_mx);
            companion_request_list(&shown, seq);
        }

        if (bsp_display_lock(100)) {
            if (dirty) {
                ui_set_now_playing(&shown, src);
                shown_src = src;
            }
            if (dirty || list_dirty) render_list(&shown);
            ui_tick(playback_elapsed_s(&shown, t), shown.duration_s);
            bsp_display_unlock();
        }

        if (on != screen_on) {
            bsp_display_brightness_set(on ? BRIGHTNESS_ON : 0);
            screen_on = on;
        }
        vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_mx = xSemaphoreCreateMutex();
    for (int i = 0; i < SRC_COUNT; i++) now_playing_init(&s_np[i]);
    arbiter_init(&s_arb);
    standby_init(&s_standby, now_ms());

    /* The ESP32-C6 radio is powered through the IO expander; bring the link up
     * before the display (see the Task 1 notes in the README). */
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_WIFI, true));
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .flags = {.buff_dma = false, .buff_spiram = true, .sw_rotate = true},
    };
    cfg.lvgl_port_cfg.task_stack = 32768;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_brightness_set(0);
    bsp_display_lock(0);
    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
    ui_init(on_ui_cmd);
    ui_on_thumb_needed(on_thumb_needed);
    lv_indev_add_event_cb(bsp_display_get_input_dev(), on_touch, LV_EVENT_PRESSED, NULL);
    bsp_display_unlock();

    s_rows = heap_caps_calloc(UI_MAX_ROWS, sizeof(ui_row_t), MALLOC_CAP_SPIRAM);
    played_set_clear(&s_played);
    static const companion_callbacks_t companion_cb = {.on_list = on_list, .on_cover = on_cover, .on_thumb = on_thumb};
    ESP_ERROR_CHECK(companion_start(&companion_cb));

    ESP_ERROR_CHECK(ams_ble_start(on_ams_update, on_ams_conn));
    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
}
