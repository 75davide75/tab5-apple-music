#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AMS_MAX_PEERS 2

enum { AMS_CMD_PLAY = 0, AMS_CMD_PAUSE = 1, AMS_CMD_TOGGLE = 2, AMS_CMD_NEXT = 3, AMS_CMD_PREV = 4 };

/* slot: 0..AMS_MAX_PEERS-1, one per connected iPhone/iPad. Callbacks run on the NimBLE host task. */
typedef void (*ams_raw_cb_t)(uint8_t slot, uint8_t entity, uint8_t attr, uint8_t flags,
                             const char *value, size_t len);
typedef void (*ams_conn_cb_t)(uint8_t slot, bool connected);

esp_err_t ams_ble_start(ams_raw_cb_t on_update, ams_conn_cb_t on_conn);
esp_err_t ams_ble_send_command(uint8_t slot, uint8_t cmd);
