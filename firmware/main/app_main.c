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
#include "lyrics.h"
#include "mac_state.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "ams_ble.h"
#include "ams_parse.h"
#include "companion.h"
#include "playback_clock.h"
#include "source_arbiter.h"
#include "standby.h"
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

/* Lyrics of the current track (guarded by s_mx). */
static companion_track_t *s_track;
static bool s_track_dirty;
static bool s_track_missing;        /* the last request got no answer from the Mac */
static uint32_t s_req_seq;          /* id of the latest companion request; older answers are dropped */

/* The highlighted line changes this long before its timestamp, to make up for drawing time. */
#define LYRIC_LEAD_S 0.25f

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
        uint32_t seq = s_np[slot].track_seq;
        now_playing_init(&s_np[slot]);
        s_np[slot].track_seq = seq + 1;      /* whatever plays after a reconnect is a new track to us */
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

    if (src == SRC_MAC) {
        static const companion_mac_cmd_t mac[] = {[UI_CMD_TOGGLE] = COMPANION_MAC_TOGGLE, [UI_CMD_NEXT] = COMPANION_MAC_NEXT,
                                                  [UI_CMD_PREV] = COMPANION_MAC_PREV, [UI_CMD_SEEK] = COMPANION_MAC_SEEK};
        companion_mac_command(mac[cmd], seek_s);
    } else if (src == SRC_IOS_0 || src == SRC_IOS_1) {
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

static void on_track(uint32_t seq, companion_track_t *info)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (seq != s_req_seq) {              /* the track changed while we were asking */
        xSemaphoreGive(s_mx);
        companion_track_free(info);
        return;
    }
    companion_track_t *old = s_track;
    s_track = info;
    s_track_dirty = true;
    s_track_missing = (info == NULL);
    xSemaphoreGive(s_mx);
    ESP_LOGI(TAG, "track info %u: %s", (unsigned)seq,
             !info ? "Mac unreachable" : !info->has_lyrics ? "no lyrics" : info->synced ? "synced lyrics" : "plain lyrics");
    companion_track_free(old);
}

static void on_cover(uint32_t seq, uint8_t *cover)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool current = (seq == s_req_seq);
    xSemaphoreGive(s_mx);
    ESP_LOGI(TAG, "cover %u: %s%s", (unsigned)seq, cover ? "ok" : "none", current ? "" : " (stale, dropped)");
    if (current && bsp_display_lock(1000)) {
        ui_set_cover(cover, UI_COVER_PX, UI_COVER_PX);
        bsp_display_unlock();
    }
    companion_free(cover);
}

/* The Mac's own Music app, polled about once a second. */
static void on_mac(bool reachable, const char *state, const char *title, const char *artist, const char *album,
                   float duration_s, float elapsed_s)
{
    static int failures;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (reachable) {
        failures = 0;
        if (mac_state_apply(&s_np[SRC_MAC], state, title, artist, album, duration_s, elapsed_s, now_ms())) s_dirty = true;
        arbiter_report(&s_arb, SRC_MAC, s_np[SRC_MAC].state, now_ms());
    } else if (++failures == 3) {            /* three missed polls: the Mac is gone */
        uint32_t seq = s_np[SRC_MAC].track_seq;
        now_playing_init(&s_np[SRC_MAC]);
        s_np[SRC_MAC].track_seq = seq + 1;
        arbiter_disconnected(&s_arb, SRC_MAC);
        s_dirty = true;
    }
    xSemaphoreGive(s_mx);
}

/* Shows the lyrics and background that just arrived. Call with the display lock held. */
static void render_track(void)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    s_track_dirty = false;
    if (s_track && s_track->has_lyrics) ui_set_lyrics(s_track->lines, s_track->n, s_track->synced);
    else ui_set_lyrics(NULL, 0, false);
    if (s_track && s_track->has_bg) ui_set_background((const uint8_t *)s_track->bg, COMPANION_BG_W, COMPANION_BG_H);
    else ui_set_background(NULL, 0, 0);
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
    int64_t changed_ms = 0, retry_ms = 0;
    bool ask_pending = false;
    /* Work for the screen stays pending until the display lock is actually obtained. */
    bool dirty = true, new_track = false;

    for (;;) {
        int64_t t = now_ms();
        xSemaphoreTake(s_mx, portMAX_DELAY);
        source_t src = arbiter_active(&s_arb);
        dirty = dirty || s_dirty || src != shown_src;
        s_dirty = false;
        if (src != SRC_NONE) shown = s_np[src];
        else if (shown.state == PB_PLAYING) shown.state = PB_PAUSED;   /* source went away */
        standby_on_playback(&s_standby, src == SRC_NONE ? PB_STOPPED : shown.state, t);
        bool on = standby_screen_on(&s_standby, t);
        bool track_dirty = s_track_dirty;
        bool track_missing = s_track_missing;
        xSemaphoreGive(s_mx);

        /* A track change arrives as several separate updates (artist, album, title, duration):
         * wait until they settle before asking the Mac about it. */
        if (src != SRC_NONE && (shown.track_seq != asked_seq || src != asked_src)) {
            asked_seq = shown.track_seq;
            asked_src = src;
            changed_ms = t;
            ask_pending = true;
            new_track = true;
            /* Answers still on their way belong to the previous track: make them stale now. */
            xSemaphoreTake(s_mx, portMAX_DELAY);
            ++s_req_seq;
            companion_track_t *old = s_track;
            s_track = NULL;
            s_track_dirty = false;
            s_track_missing = false;
            track_dirty = false;
            track_missing = false;
            xSemaphoreGive(s_mx);
            companion_track_free(old);
        }
        /* The Mac was unreachable last time: ask again once it is back. */
        if (track_missing && !ask_pending && src != SRC_NONE && companion_available() && t - retry_ms > 5000) {
            retry_ms = t;
            ask_pending = true;
        }
        if (ask_pending && t - changed_ms >= 500 && shown.title[0]) {
            ask_pending = false;
            xSemaphoreTake(s_mx, portMAX_DELAY);
            uint32_t seq = ++s_req_seq;
            xSemaphoreGive(s_mx);
            companion_request_track(&shown, seq);
        }

        float elapsed = playback_elapsed_s(&shown, t);
        if (bsp_display_lock(100)) {
            if (dirty) {
                ui_set_now_playing(&shown, src);
                shown_src = src;
                dirty = false;
            }
            if (new_track) {               /* the old cover and lyrics belong to the previous track */
                ui_set_cover(NULL, 0, 0);
                ui_clear_lyrics();
                new_track = false;
            }
            if (track_dirty) render_track();
            xSemaphoreTake(s_mx, portMAX_DELAY);
            int line = (s_track && s_track->synced && !s_track_dirty)
                           ? lyrics_current_line(s_track->times, s_track->n, elapsed + LYRIC_LEAD_S) : -2;
            xSemaphoreGive(s_mx);
            if (line != -2) ui_set_lyric_line(line);
            ui_tick(elapsed, shown.duration_s);
            bsp_display_unlock();
        }

        if (on != screen_on) {
            /* ramp over about 400 ms instead of snapping */
            for (int step = 1; step <= 8; step++) {
                int level = BRIGHTNESS_ON * step / 8;
                bsp_display_brightness_set(on ? level : BRIGHTNESS_ON - level);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
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
    lv_indev_add_event_cb(bsp_display_get_input_dev(), on_touch, LV_EVENT_PRESSED, NULL);
    bsp_display_unlock();

    static const companion_callbacks_t companion_cb = {.on_track = on_track, .on_cover = on_cover, .on_mac = on_mac};
    ESP_ERROR_CHECK(companion_start(&companion_cb));

    ESP_ERROR_CHECK(ams_ble_start(on_ams_update, on_ams_conn));
    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
}
