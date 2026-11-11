#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "ams_ble.h"
#include "ams_parse.h"
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

static void ui_task(void *arg)
{
    now_playing_t shown;
    now_playing_init(&shown);
    source_t shown_src = SRC_NONE;
    bool screen_on = false;

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
        xSemaphoreGive(s_mx);

        if (bsp_display_lock(100)) {
            if (dirty) {
                ui_set_now_playing(&shown, src);
                char hdr[48] = "";
                if (shown.queue_index >= 0 && shown.queue_count > 0) {
                    snprintf(hdr, sizeof hdr, "Brano %d di %d", shown.queue_index + 1, shown.queue_count);
                }
                ui_set_list(hdr, "", shown.shuffle, NULL, 0);
                shown_src = src;
            }
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
    lv_indev_add_event_cb(bsp_display_get_input_dev(), on_touch, LV_EVENT_PRESSED, NULL);
    bsp_display_unlock();

    ESP_ERROR_CHECK(ams_ble_start(on_ams_update, on_ams_conn));
    xTaskCreate(ui_task, "ui", 8192, NULL, 4, NULL);
}
