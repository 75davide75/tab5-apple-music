/* Task 1 spike: show raw Apple Media Service data from a paired iPhone. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "ams_ble.h"

static const char *TAG = "spike";
static lv_obj_t *s_lbl[8];
enum { L_CONN, L_PLAYER, L_TITLE, L_ARTIST, L_INFO, L_QUEUE, L_SHUFFLE, L_DUR };
static const char *s_names[] = {"Connessione", "Lettore", "Titolo", "Artista", "PlaybackInfo",
                                "Coda", "Casuale", "Durata"};
static char s_q_index[16] = "?", s_q_count[16] = "?";
static uint8_t s_slot;

static void set_label(int i, const char *value)
{
    if (bsp_display_lock(100)) {
        lv_label_set_text_fmt(s_lbl[i], "%s: %s", s_names[i], value);
        bsp_display_unlock();
    }
}

static void on_update(uint8_t slot, uint8_t entity, uint8_t attr, uint8_t flags,
                      const char *value, size_t len)
{
    char v[256];
    if (len >= sizeof v) len = sizeof v - 1;
    memcpy(v, value, len);
    v[len] = 0;
    ESP_LOGI(TAG, "slot=%d entity=%d attr=%d flags=%d value='%s'", slot, entity, attr, flags, v);
    s_slot = slot;
    if (entity == 0 && attr == 0) set_label(L_PLAYER, v);
    else if (entity == 0 && attr == 1) set_label(L_INFO, v);
    else if (entity == 1 && (attr == 0 || attr == 1)) {
        snprintf(attr == 0 ? s_q_index : s_q_count, 16, "%.15s", v);
        char q[48];
        snprintf(q, sizeof q, "indice %s di %s", s_q_index, s_q_count);
        set_label(L_QUEUE, q);
    } else if (entity == 1 && attr == 2) set_label(L_SHUFFLE, v);
    else if (entity == 2 && attr == 0) set_label(L_ARTIST, v);
    else if (entity == 2 && attr == 2) set_label(L_TITLE, v);
    else if (entity == 2 && attr == 3) set_label(L_DUR, v);
}

static void on_conn(uint8_t slot, bool connected)
{
    char s[32];
    snprintf(s, sizeof s, "%s (slot %d)", connected ? "AMS pronto" : "scollegato", slot);
    set_label(L_CONN, s);
}

static void btn_cb(lv_event_t *e)
{
    uint8_t cmd = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "command %d -> %s", cmd, esp_err_to_name(ams_ble_send_command(s_slot, cmd)));
}

static void make_button(lv_obj_t *parent, const char *text, uint8_t cmd, int x)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 280, 110);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, x, -30);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* The ESP32-C6 radio is powered through the IO expander. */
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
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    bsp_display_lock(0);
    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
    lv_obj_t *scr = lv_screen_active();
    for (int i = 0; i < 8; i++) {
        s_lbl[i] = lv_label_create(scr);
        lv_obj_set_style_text_font(s_lbl[i], &lv_font_montserrat_28, 0);
        lv_label_set_text_fmt(s_lbl[i], "%s: -", s_names[i]);
        lv_obj_align(s_lbl[i], LV_ALIGN_TOP_LEFT, 30, 30 + i * 58);
    }
    make_button(scr, "Play / Pausa", AMS_CMD_TOGGLE, 30);
    make_button(scr, "Avanti", AMS_CMD_NEXT, 340);
    bsp_display_unlock();

    ESP_ERROR_CHECK(ams_ble_start(on_update, on_conn));
    set_label(L_CONN, "in attesa di abbinamento");
}
