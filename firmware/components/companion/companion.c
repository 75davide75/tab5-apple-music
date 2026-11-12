#include "companion.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "text_util.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

#define COVER_BYTES (400 * 400 * 2)
#define THUMB_BYTES (52 * 52 * 2)
#define BG_BYTES (160 * 90 * 2)
#define REDISCOVER_MS 10000

static const char *TAG = "companion";

typedef enum { JOB_LIST, JOB_COVER, JOB_THUMB } job_kind_t;
typedef struct {
    job_kind_t kind;
    uint32_t seq, track_id;
    bool has_track_id;
    char *query;   /* heap; title/artist/album already URL-encoded */
} job_t;

static companion_callbacks_t s_cb;
static QueueHandle_t s_urgent, s_thumbs;
static volatile bool s_have_ip, s_available;
static char s_base[64];
static int s_failures;

void companion_free(void *p) { heap_caps_free(p); }

void companion_list_free(companion_list_t *list)
{
    if (!list) return;
    if (list->json) cJSON_Delete(list->json);
    heap_caps_free(list->tracks);
    heap_caps_free(list);
}

bool companion_available(void) { return s_available; }

/* The companion's last known address is remembered so that a restart does not
 * have to wait for mDNS; if it stops answering we fall back to discovery. */
static void load_base(void)
{
    nvs_handle_t h;
    size_t len = sizeof s_base;
    if (nvs_open("companion", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_str(h, "base", s_base, &len) != ESP_OK) s_base[0] = 0;
    nvs_close(h);
}

static void save_base(void)
{
    nvs_handle_t h;
    if (nvs_open("companion", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "base", s_base);
    nvs_commit(h);
    nvs_close(h);
}

/* ---- Wi-Fi ---- */

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_have_ip = false;
        s_available = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying",
                 data ? ((wifi_event_sta_disconnected_t *)data)->reason : -1);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected");
        s_have_ip = true;
        if (s_base[0]) { s_failures = 0; s_available = true; }   /* try the remembered address first */
    }
}

/* ---- HTTP ---- */

typedef struct { uint8_t *buf; size_t len, cap; bool overflow; } body_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    body_t *b = e->user_data;
    if (b->len + e->data_len > b->cap) { b->overflow = true; return ESP_OK; }
    memcpy(b->buf + b->len, e->data, e->data_len);
    b->len += e->data_len;
    return ESP_OK;
}

/* GETs base+path into a PSRAM buffer of at most cap bytes. Returns NULL unless the status is 200. */
static uint8_t *http_get(const char *path, size_t cap, size_t *len, int timeout_ms)
{
    char url[1400];
    snprintf(url, sizeof url, "%s%s", s_base, path);
    body_t body = {.buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM), .cap = cap};
    if (!body.buf) return NULL;

    esp_http_client_config_t cfg = {.url = url, .timeout_ms = timeout_ms, .event_handler = on_http,
                                    .user_data = &body, .buffer_size = 4096};
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = c ? esp_http_client_perform(c) : ESP_FAIL;
    int status = c ? esp_http_client_get_status_code(c) : 0;
    if (c) esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        if (++s_failures >= 2) s_available = false;   /* go back to discovery */
    } else {
        s_failures = 0;
    }
    if (err != ESP_OK || status != 200 || body.overflow) {
        heap_caps_free(body.buf);
        return NULL;
    }
    body.buf[body.len] = 0;
    if (len) *len = body.len;
    return body.buf;
}

static uint8_t *get_pixels(const char *path, size_t bytes, int timeout_ms)
{
    size_t len = 0;
    uint8_t *px = http_get(path, bytes, &len, timeout_ms);
    if (px && len != bytes) { heap_caps_free(px); px = NULL; }
    return px;
}

/* ---- jobs ---- */

static companion_list_t *parse_list(char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (!root) return NULL;
    companion_list_t *l = heap_caps_calloc(1, sizeof *l, MALLOC_CAP_SPIRAM);
    if (!l) { cJSON_Delete(root); return NULL; }
    l->json = root;

    const cJSON *pl = cJSON_GetObjectItem(root, "playlist");
    if (cJSON_IsObject(pl)) {
        const cJSON *name = cJSON_GetObjectItem(pl, "name");
        l->has_playlist = true;
        snprintf(l->name, sizeof l->name, "%s", cJSON_IsString(name) ? name->valuestring : "");
        l->count = cJSON_GetObjectItem(pl, "count") ? cJSON_GetObjectItem(pl, "count")->valueint : 0;
    }
    const cJSON *cur = cJSON_GetObjectItem(root, "current");
    l->current = cJSON_IsNumber(cur) ? cur->valueint : 0;
    l->ordered = cJSON_IsTrue(cJSON_GetObjectItem(root, "ordered"));
    const cJSON *tid = cJSON_GetObjectItem(root, "track_id");
    if (cJSON_IsNumber(tid)) { l->has_track_id = true; l->track_id = (uint32_t)tid->valuedouble; }

    const cJSON *tracks = cJSON_GetObjectItem(root, "tracks");
    int n = cJSON_IsArray(tracks) ? cJSON_GetArraySize(tracks) : 0;
    if (n > 0) l->tracks = heap_caps_calloc(n, sizeof *l->tracks, MALLOC_CAP_SPIRAM);
    const cJSON *t;
    cJSON_ArrayForEach(t, tracks) {
        if (!l->tracks || l->n >= n) break;
        const cJSON *id = cJSON_GetObjectItem(t, "id"), *title = cJSON_GetObjectItem(t, "title");
        const cJSON *artist = cJSON_GetObjectItem(t, "artist"), *dur = cJSON_GetObjectItem(t, "duration_s");
        if (!cJSON_IsNumber(id)) continue;
        companion_track_t *o = &l->tracks[l->n++];
        o->id = (uint32_t)id->valuedouble;
        o->title = cJSON_IsString(title) ? title->valuestring : "";
        o->artist = cJSON_IsString(artist) ? artist->valuestring : "";
        o->duration_s = cJSON_IsNumber(dur) ? (float)dur->valuedouble : 0;
    }
    return l;
}

static void art_path(char *out, size_t cap, const char *endpoint, const job_t *j, int size)
{
    int n = j->has_track_id ? snprintf(out, cap, "%s?id=%u", endpoint, (unsigned)j->track_id)
                            : snprintf(out, cap, "%s?%s", endpoint, j->query ? j->query : "");
    if (size && n > 0 && (size_t)n < cap) snprintf(out + n, cap - n, "&size=%d", size);
}

static void run_job(job_t *j)
{
    char path[1300];
    switch (j->kind) {
    case JOB_LIST: {
        companion_list_t *list = NULL;
        if (s_available) {
            snprintf(path, sizeof path, "/v1/resolve?%s", j->query ? j->query : "");
            char *text = (char *)http_get(path, 256 * 1024, NULL, 3000);
            if (text) { list = parse_list(text); heap_caps_free(text); }
        }
        if (s_cb.on_list) s_cb.on_list(j->seq, list);
        else companion_list_free(list);
        break;
    }
    case JOB_COVER: {
        uint8_t *cover = NULL, *bg = NULL;
        if (s_available) {
            art_path(path, sizeof path, "/v1/art", j, 400);
            cover = get_pixels(path, COVER_BYTES, 8000);
            if (cover) {
                art_path(path, sizeof path, "/v1/bg", j, 0);
                bg = get_pixels(path, BG_BYTES, 4000);
            }
        }
        if (s_cb.on_cover) s_cb.on_cover(j->seq, cover, bg);
        else { heap_caps_free(cover); heap_caps_free(bg); }
        break;
    }
    case JOB_THUMB: {
        uint8_t *px = NULL;
        if (s_available) {
            snprintf(path, sizeof path, "/v1/art?id=%u&size=52", (unsigned)j->track_id);
            px = get_pixels(path, THUMB_BYTES, 4000);
        }
        if (s_cb.on_thumb) s_cb.on_thumb(j->track_id, px);
        else heap_caps_free(px);
        break;
    }
    }
    free(j->query);
}

static void discover(void)
{
    mdns_result_t *r = NULL;
    esp_err_t err = mdns_query_ptr("_tab5music", "_tcp", 3000, 4, &r);
    if (err != ESP_OK || !r) {
        ESP_LOGI(TAG, "companion not found yet (mdns: %s)", esp_err_to_name(err));
        return;
    }
    for (mdns_result_t *it = r; it; it = it->next) {
        ESP_LOGI(TAG, "mdns answer: host=%s port=%u addr=%s", it->hostname ? it->hostname : "-", it->port,
                 it->addr ? "yes" : "no");
        esp_ip4_addr_t ip = {0};
        bool have_ip = false;
        for (mdns_ip_addr_t *a = it->addr; a && !have_ip; a = a->next) {
            if (a->addr.type == ESP_IPADDR_TYPE_V4) { ip = a->addr.u_addr.ip4; have_ip = true; }
        }
        /* The answer often carries only the host name: resolve it separately. */
        if (!have_ip && it->hostname) have_ip = (mdns_query_a(it->hostname, 2000, &ip) == ESP_OK);
        if (!have_ip) continue;
        snprintf(s_base, sizeof s_base, "http://" IPSTR ":%u", IP2STR(&ip), it->port);
        s_failures = 0;
        s_available = true;
        save_base();
        ESP_LOGI(TAG, "companion found at %s", s_base);
        mdns_query_results_free(r);
        return;
    }
    mdns_query_results_free(r);
}

static void worker(void *arg)
{
    TickType_t last_discovery = 0;
    for (;;) {
        if (s_have_ip && !s_available &&
            (last_discovery == 0 || xTaskGetTickCount() - last_discovery > pdMS_TO_TICKS(REDISCOVER_MS))) {
            discover();
            last_discovery = xTaskGetTickCount();
        }
        job_t j;
        if (xQueueReceive(s_urgent, &j, 0) == pdTRUE || xQueueReceive(s_thumbs, &j, pdMS_TO_TICKS(200)) == pdTRUE) {
            run_job(&j);
        }
    }
}

/* ---- public API ---- */

esp_err_t companion_start(const companion_callbacks_t *cb)
{
    s_cb = *cb;
    s_urgent = xQueueCreate(8, sizeof(job_t));
    s_thumbs = xQueueCreate(96, sizeof(job_t));
    if (!s_urgent || !s_thumbs) return ESP_ERR_NO_MEM;

    load_base();
    if (strlen(WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "no Wi-Fi credentials (main/wifi_secrets.h): running without the companion");
    } else {
        esp_netif_create_default_wifi_sta();
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
        wifi_config_t wc = {0};
        snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", WIFI_SSID);
        snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", WIFI_PASSWORD);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
        ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_init());
    }
    return xTaskCreate(worker, "companion", 12288, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static char *build_query(const now_playing_t *np)
{
    char t[800], a[800], al[800];
    if (!url_encode(np->title, t, sizeof t)) t[0] = 0;
    if (!url_encode(np->artist, a, sizeof a)) a[0] = 0;
    if (!url_encode(np->album, al, sizeof al)) al[0] = 0;
    char *q = malloc(1200);
    if (!q) return NULL;
    int n = snprintf(q, 1200, "title=%.400s&artist=%.300s&album=%.300s&shuffle=%d", t, a, al, np->shuffle ? 1 : 0);
    if (np->queue_index >= 0) n += snprintf(q + n, 1200 - n, "&index=%d", np->queue_index);
    if (np->queue_count > 0) snprintf(q + n, 1200 - n, "&count=%d", np->queue_count);
    return q;
}

static void push(QueueHandle_t q, job_t *j)
{
    if (xQueueSend(q, j, 0) != pdTRUE) free(j->query);
}

void companion_request_list(const now_playing_t *np, uint32_t seq)
{
    job_t j = {.kind = JOB_LIST, .seq = seq, .query = build_query(np)};
    push(s_urgent, &j);
}

void companion_request_cover(bool has_track_id, uint32_t track_id, const now_playing_t *np, uint32_t seq)
{
    job_t j = {.kind = JOB_COVER, .seq = seq, .has_track_id = has_track_id, .track_id = track_id,
               .query = has_track_id ? NULL : build_query(np)};
    push(s_urgent, &j);
}

void companion_request_thumb(uint32_t track_id)
{
    job_t j = {.kind = JOB_THUMB, .track_id = track_id};
    push(s_thumbs, &j);
}
