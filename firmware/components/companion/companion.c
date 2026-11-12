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
#include "jpeg_decoder.h"
#include "lyrics.h"
#include "mdns.h"
#include "nvs.h"
#include "text_util.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

#define COVER_BYTES (COMPANION_COVER_PX * COMPANION_COVER_PX * 2)
#define REDISCOVER_MS 10000
/* Measured on this Tab5 (factory ESP32-C6 firmware): packets that arrive in bursts get
 * lost, which made downloads crawl at ~4 kB/s. sdkconfig.defaults therefore keeps the
 * TCP receive window at two segments (~20 kB/s); answers are still kept small. */
#define HTTP_TIMEOUT_MS 15000
#define MAX_JPEG_BYTES (96 * 1024)
#define MAX_LINES 300

static const char *TAG = "companion";

typedef enum { JOB_TRACK, JOB_COVER } job_kind_t;
typedef struct {
    job_kind_t kind;
    uint32_t seq;
    char *query;   /* heap; title/artist/album already URL-encoded */
} job_t;

static companion_callbacks_t s_cb;
static QueueHandle_t s_jobs;
static volatile bool s_have_ip, s_available;
static char s_base[64];
static int s_failures;

void companion_free(void *p) { heap_caps_free(p); }

void companion_track_free(companion_track_t *t)
{
    if (!t) return;
    if (t->json) cJSON_Delete(t->json);
    heap_caps_free(t->lines);
    heap_caps_free(t->times);
    heap_caps_free(t);
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
        ESP_LOGW(TAG, "GET %.60s failed: %s, status %d, %u bytes%s", path, esp_err_to_name(err), status,
                 (unsigned)body.len, body.overflow ? ", too large" : "");
        heap_caps_free(body.buf);
        return NULL;
    }
    body.buf[body.len] = 0;
    if (len) *len = body.len;
    return body.buf;
}

/* ---- jobs ---- */

static companion_track_t *parse_track(char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (!root) return NULL;
    companion_track_t *t = heap_caps_calloc(1, sizeof *t, MALLOC_CAP_SPIRAM);
    if (!t) { cJSON_Delete(root); return NULL; }
    t->json = root;

    const cJSON *bg = cJSON_GetObjectItem(root, "bg");
    if (cJSON_IsString(bg)) {
        t->has_bg = hex_to_rgb565(bg->valuestring, t->bg, COMPANION_BG_W * COMPANION_BG_H) ==
                    COMPANION_BG_W * COMPANION_BG_H;
    }

    const cJSON *lyrics = cJSON_GetObjectItem(root, "lyrics");
    const cJSON *lines = cJSON_IsObject(lyrics) ? cJSON_GetObjectItem(lyrics, "lines") : NULL;
    int n = cJSON_IsArray(lines) ? cJSON_GetArraySize(lines) : 0;
    if (n > MAX_LINES) n = MAX_LINES;
    if (n > 0) {
        t->lines = heap_caps_calloc(n, sizeof *t->lines, MALLOC_CAP_SPIRAM);
        t->times = heap_caps_calloc(n, sizeof *t->times, MALLOC_CAP_SPIRAM);
    }
    if (t->lines && t->times) {
        const cJSON *line;
        float last = -1;
        cJSON_ArrayForEach(line, lines) {
            if (t->n >= n) break;
            const cJSON *time = cJSON_GetArrayItem(line, 0), *words = cJSON_GetArrayItem(line, 1);
            if (!cJSON_IsNumber(time) || !cJSON_IsString(words)) continue;
            float at = (float)time->valuedouble;
            if (at < last) at = last;          /* keep times ascending whatever we are sent */
            last = at;
            t->times[t->n] = at;
            t->lines[t->n] = words->valuestring;
            t->n++;
        }
        t->has_lyrics = t->n > 0;
        t->synced = t->has_lyrics && cJSON_IsTrue(cJSON_GetObjectItem(lyrics, "synced"));
    }
    return t;
}

/* The decoder needs a scratch area; its default (3.1 kB) is too small for the JPEGs macOS writes. */
#define JPEG_WORK_BYTES (70 * 1024)

static uint8_t *decode_cover(const uint8_t *jpeg, size_t len)
{
    uint8_t *out = heap_caps_malloc(COVER_BYTES, MALLOC_CAP_SPIRAM);
    void *work = heap_caps_malloc(JPEG_WORK_BYTES, MALLOC_CAP_SPIRAM);
    if (!out || !work) {
        heap_caps_free(out);
        heap_caps_free(work);
        return NULL;
    }
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpeg, .indata_size = len, .outbuf = out, .outbuf_size = COVER_BYTES,
        .out_format = JPEG_IMAGE_FORMAT_RGB565, .out_scale = JPEG_IMAGE_SCALE_0,
        .advanced = {.working_buffer = work, .working_buffer_size = JPEG_WORK_BYTES},
    };
    esp_jpeg_image_output_t info = {0};
    esp_err_t err = esp_jpeg_decode(&cfg, &info);
    heap_caps_free(work);
    if (err != ESP_OK || info.width != COMPANION_COVER_PX || info.height != COMPANION_COVER_PX) {
        ESP_LOGW(TAG, "cover decode failed: %s, %ux%u, %u bytes of JPEG", esp_err_to_name(err), info.width,
                 info.height, (unsigned)len);
        heap_caps_free(out);
        return NULL;
    }
    return out;
}

static void run_job(job_t *j)
{
    char path[1300];
    switch (j->kind) {
    case JOB_TRACK: {
        companion_track_t *info = NULL;
        if (s_available) {
            snprintf(path, sizeof path, "/v1/track?%s", j->query ? j->query : "");
            char *text = (char *)http_get(path, 96 * 1024, NULL, HTTP_TIMEOUT_MS);
            if (text) { info = parse_track(text); heap_caps_free(text); }
        }
        if (s_cb.on_track) s_cb.on_track(j->seq, info);
        else companion_track_free(info);
        break;
    }
    case JOB_COVER: {
        uint8_t *cover = NULL;
        if (s_available) {
            size_t len = 0;
            snprintf(path, sizeof path, "/v1/cover.jpg?%s", j->query ? j->query : "");
            uint8_t *jpeg = http_get(path, MAX_JPEG_BYTES, &len, HTTP_TIMEOUT_MS);
            if (jpeg) { cover = decode_cover(jpeg, len); heap_caps_free(jpeg); }
        }
        if (s_cb.on_cover) s_cb.on_cover(j->seq, cover);
        else heap_caps_free(cover);
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

#define LINK_TEST 0   /* set to 1 to time a few downloads from the companion at start-up */
#if LINK_TEST
#include "esp_timer.h"
static void link_test(void)
{
    static const int sizes[] = {8000, 20000, 32000, 20000};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        char path[48];
        snprintf(path, sizeof path, "/v1/blob?n=%d", sizes[i]);
        int64_t t0 = esp_timer_get_time();
        size_t len = 0;
        uint8_t *b = http_get(path, 400000, &len, 8000);
        ESP_LOGI(TAG, "link test %d bytes: %s in %d ms", sizes[i], b ? "ok" : "FAILED",
                 (int)((esp_timer_get_time() - t0) / 1000));
        heap_caps_free(b);
    }
}
#endif

static void worker(void *arg)
{
    TickType_t last_discovery = 0;
#if LINK_TEST
    bool tested = false;
#endif
    for (;;) {
#if LINK_TEST
        if (s_available && !tested) { tested = true; link_test(); }
#endif
        if (s_have_ip && !s_available &&
            (last_discovery == 0 || xTaskGetTickCount() - last_discovery > pdMS_TO_TICKS(REDISCOVER_MS))) {
            discover();
            last_discovery = xTaskGetTickCount();
        }
        job_t j;
        if (xQueueReceive(s_jobs, &j, pdMS_TO_TICKS(200)) == pdTRUE) {
            /* A newer track makes anything still waiting pointless: only its jobs are worth the slow link. */
            job_t newer;
            while (j.kind == JOB_COVER && xQueuePeek(s_jobs, &newer, 0) == pdTRUE && newer.seq != j.seq) {
                free(j.query);
                xQueueReceive(s_jobs, &j, 0);
            }
            run_job(&j);
        }
    }
}

/* ---- public API ---- */

esp_err_t companion_start(const companion_callbacks_t *cb)
{
    s_cb = *cb;
    s_jobs = xQueueCreate(8, sizeof(job_t));
    if (!s_jobs) return ESP_ERR_NO_MEM;

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
    snprintf(q, 1200, "title=%.400s&artist=%.300s&album=%.300s&duration=%.1f", t, a, al,
             np->duration_s > 0 ? (double)np->duration_s : 0.0);
    return q;
}

void companion_request_track(const now_playing_t *np, uint32_t seq)
{
    job_t track = {.kind = JOB_TRACK, .seq = seq, .query = build_query(np)};
    if (xQueueSend(s_jobs, &track, 0) != pdTRUE) free(track.query);
    job_t cover = {.kind = JOB_COVER, .seq = seq, .query = build_query(np)};
    if (xQueueSend(s_jobs, &cover, 0) != pdTRUE) free(cover.query);
}
