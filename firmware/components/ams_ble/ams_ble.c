#include "ams_ble.h"
#include <string.h>
#include "esp_log.h"
#include "esp_hosted.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ams_ble";

/* Apple Media Service UUIDs (little-endian byte order) */
static const ble_uuid128_t UUID_AMS = BLE_UUID128_INIT(
    0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89);
static const ble_uuid128_t UUID_REMOTE_CMD = BLE_UUID128_INIT(
    0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8, 0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B);
static const ble_uuid128_t UUID_ENTITY_UPDATE = BLE_UUID128_INIT(
    0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A, 0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F);
static const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4, 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);

typedef struct {
    bool used;
    bool ready;
    uint16_t conn;
    uint16_t svc_start, svc_end;
    uint16_t h_cmd, h_update, h_cccd;
    uint8_t sub_step;
} peer_t;

static peer_t s_peers[AMS_MAX_PEERS];
static ams_raw_cb_t s_on_update;
static ams_conn_cb_t s_on_conn;
static uint8_t s_own_addr_type;

void ble_store_config_init(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static void subscribe_next(peer_t *p);

static int slot_of(const peer_t *p) { return (int)(p - s_peers); }

static peer_t *peer_by_conn(uint16_t conn)
{
    for (int i = 0; i < AMS_MAX_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].conn == conn) return &s_peers[i];
    }
    return NULL;
}

static peer_t *peer_alloc(uint16_t conn)
{
    for (int i = 0; i < AMS_MAX_PEERS; i++) {
        if (!s_peers[i].used) {
            memset(&s_peers[i], 0, sizeof(peer_t));
            s_peers[i].used = true;
            s_peers[i].conn = conn;
            return &s_peers[i];
        }
    }
    return NULL;
}

static bool have_free_slot(void)
{
    for (int i = 0; i < AMS_MAX_PEERS; i++) if (!s_peers[i].used) return true;
    return false;
}

static void advertise(void)
{
    if (!have_free_slot() || ble_gap_adv_active()) return;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.sol_uuids128 = &UUID_ANCS;
    fields.sol_num_uuids128 = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "adv_set_fields rc=%d", rc); return; }

    struct ble_hs_adv_fields rsp = {0};
    const char *name = ble_svc_gap_device_name();
    rsp.name = (const uint8_t *)name;
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
    rsp.sol_uuids128 = &UUID_AMS;
    rsp.sol_num_uuids128 = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) { ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc); return; }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "adv_start rc=%d", rc);
    else ESP_LOGI(TAG, "advertising");
}

/* ---- AMS discovery and subscription chain ---- */

static int on_write_done(uint16_t conn, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) return 0;
    if (error->status != 0) {
        ESP_LOGE(TAG, "write step %d failed status=%d", p->sub_step, error->status);
        return 0;
    }
    p->sub_step++;
    subscribe_next(p);
    return 0;
}

static void subscribe_next(peer_t *p)
{
    /* step 0: enable notifications; 1..3: register for Player, Queue, Track attributes */
    static const uint8_t cccd[] = {0x01, 0x00};
    static const uint8_t ent_player[] = {0, 0, 1};
    static const uint8_t ent_queue[] = {1, 0, 1, 2};
    static const uint8_t ent_track[] = {2, 0, 1, 2, 3};
    int rc = 0;
    switch (p->sub_step) {
    case 0: rc = ble_gattc_write_flat(p->conn, p->h_cccd, cccd, sizeof cccd, on_write_done, NULL); break;
    case 1: rc = ble_gattc_write_flat(p->conn, p->h_update, ent_player, sizeof ent_player, on_write_done, NULL); break;
    case 2: rc = ble_gattc_write_flat(p->conn, p->h_update, ent_queue, sizeof ent_queue, on_write_done, NULL); break;
    case 3: rc = ble_gattc_write_flat(p->conn, p->h_update, ent_track, sizeof ent_track, on_write_done, NULL); break;
    default:
        p->ready = true;
        ESP_LOGI(TAG, "slot %d: AMS ready", slot_of(p));
        if (s_on_conn) s_on_conn(slot_of(p), true);
        return;
    }
    if (rc != 0) ESP_LOGE(TAG, "write step %d rc=%d", p->sub_step, rc);
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *error, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) return 0;
    if (error->status == 0) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16 &&
            dsc->handle > p->h_update && (p->h_cccd == 0 || dsc->handle < p->h_cccd)) {
            p->h_cccd = dsc->handle;
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE && p->h_cccd) {
        p->sub_step = 0;
        subscribe_next(p);
    } else {
        ESP_LOGE(TAG, "descriptor discovery failed status=%d cccd=%d", error->status, p->h_cccd);
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) return 0;
    if (error->status == 0) {
        char buf[BLE_UUID_STR_LEN];
        ESP_LOGI(TAG, "chr %s val_handle=%d", ble_uuid_to_str(&chr->uuid.u, buf), chr->val_handle);
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_REMOTE_CMD.u) == 0) p->h_cmd = chr->val_handle;
        if (ble_uuid_cmp(&chr->uuid.u, &UUID_ENTITY_UPDATE.u) == 0) p->h_update = chr->val_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE && p->h_cmd && p->h_update) {
        int rc = ble_gattc_disc_all_dscs(conn, p->h_update, p->svc_end, on_dsc, NULL);
        if (rc != 0) ESP_LOGE(TAG, "disc_all_dscs rc=%d", rc);
    } else {
        ESP_LOGE(TAG, "characteristic discovery failed status=%d cmd=%d update=%d",
                 error->status, p->h_cmd, p->h_update);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg)
{
    peer_t *p = peer_by_conn(conn);
    if (!p) return 0;
    if (error->status == 0) {
        p->svc_start = svc->start_handle;
        p->svc_end = svc->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE && p->svc_start) {
        ESP_LOGI(TAG, "AMS service found %d..%d", p->svc_start, p->svc_end);
        int rc = ble_gattc_disc_all_chrs(conn, p->svc_start, p->svc_end, on_chr, NULL);
        if (rc != 0) ESP_LOGE(TAG, "disc_all_chrs rc=%d", rc);
    } else {
        ESP_LOGE(TAG, "AMS service not found (status=%d)", error->status);
    }
    return 0;
}

static void start_discovery(peer_t *p)
{
    p->svc_start = p->svc_end = p->h_cmd = p->h_update = p->h_cccd = 0;
    int rc = ble_gattc_disc_svc_by_uuid(p->conn, &UUID_AMS.u, on_svc, NULL);
    if (rc != 0) ESP_LOGE(TAG, "disc_svc rc=%d", rc);
}

static void drop_peer(peer_t *p)
{
    int slot = slot_of(p);
    p->used = false;
    p->ready = false;
    /* Always report it: updates can arrive before the peer is marked ready. */
    if (s_on_conn) s_on_conn(slot, false);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    peer_t *p;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect status=%d", event->connect.status);
        if (event->connect.status == 0) {
            p = peer_alloc(event->connect.conn_handle);
            if (!p) {
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            ESP_LOGI(TAG, "security_initiate rc=%d", rc);
        }
        advertise();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
        p = peer_by_conn(event->disconnect.conn.conn_handle);
        if (p) drop_peer(p);
        advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "enc_change status=%d encrypted=%d bonded=%d", event->enc_change.status,
                     desc.sec_state.encrypted, desc.sec_state.bonded);
        }
        p = peer_by_conn(event->enc_change.conn_handle);
        if (p && event->enc_change.status == 0 && !p->ready) start_discovery(p);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        p = peer_by_conn(event->notify_rx.conn_handle);
        if (p && event->notify_rx.attr_handle == p->h_update) {
            static uint8_t buf[600];
            uint16_t len = 0;
            if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof buf, &len) == 0 && len >= 3) {
                if (s_on_update) s_on_update(slot_of(p), buf[0], buf[1], buf[2],
                                             (const char *)&buf[3], len - 3);
            }
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The phone forgot us: drop the stale bond and pair again. */
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%d", event->mtu.value);
        return 0;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) { ESP_LOGE(TAG, "ensure_addr rc=%d", rc); return; }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "infer_auto rc=%d", rc); return; }
    ESP_LOGI(TAG, "host synced, addr type %d", s_own_addr_type);
    advertise();
}

static void on_reset(int reason) { ESP_LOGE(TAG, "host reset reason=%d", reason); }

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ams_ble_start(ams_raw_cb_t on_update, ams_conn_cb_t on_conn)
{
    s_on_update = on_update;
    s_on_conn = on_conn;

    /* The Bluetooth controller lives on the ESP32-C6; bring it up over ESP-Hosted. */
    esp_hosted_connect_to_slave();
    /* The factory ESP32-C6 firmware starts its Bluetooth controller at boot and
     * does not implement the newer controller init/enable requests. */
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Tab5 Music");
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t ams_ble_send_command(uint8_t slot, uint8_t cmd)
{
    if (slot >= AMS_MAX_PEERS || !s_peers[slot].ready) return ESP_ERR_INVALID_STATE;
    int rc = ble_gattc_write_flat(s_peers[slot].conn, s_peers[slot].h_cmd, &cmd, 1, NULL, NULL);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
