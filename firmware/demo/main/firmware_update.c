#include "firmware_update.h"
#include "update_manifest.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include "esp_netif_sntp.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"

#define SLOT_SIZE 0x400000u
#define URL_CAP 4097
#define CHUNK 4096
#define LATEST "https://github.com/e7h4n/coffee-watch/releases/latest/download/latest.json"
#define LOCK() xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_worker;
static firmware_update_status_t s_status;
static bool s_cancel;
static int s_command;
static const esp_partition_t *s_ready;
static um_manifest_t s_manifest;

typedef struct {
    char location[URL_CAP];
    bool bad, location_seen, length_seen, encoding_seen;
    uint64_t length;
    int64_t deadline;
} response_t;
static bool cancelled(void) { LOCK(); bool c = s_cancel; UNLOCK(); return c; }
static bool stopped(response_t *r) { return cancelled() || esp_timer_get_time() > r->deadline; }
static void progress(uint32_t n) { LOCK(); s_status.received_bytes = n; UNLOCK(); }
static esp_err_t layout(const esp_partition_t **inactive, bool require_valid)
{
#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK)
    /* Anti-rollback mark-valid can burn eFuses. This updater never does so. */
    return ESP_ERR_NOT_SUPPORTED;
#endif
    uint32_t flash = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &flash) != ESP_OK || flash != 0x1000000) return ESP_ERR_NOT_SUPPORTED;
    const esp_partition_t *a = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *b = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *d = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    if (!a || !b || !d || a->address != 0x10000 || b->address != 0x410000 || a->size != SLOT_SIZE || b->size != SLOT_SIZE || d->address != 0x810000 || d->size != 0x2000) return ESP_ERR_NOT_SUPPORTED;
    /* Reject extra app/otadata partitions and overlaps, including data aliases. */
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        bool known = p == a || p == b || p == d;
        uint64_t end = (uint64_t)p->address + p->size;
        if (end > flash || (!known && (p->type == ESP_PARTITION_TYPE_APP || (p->type == ESP_PARTITION_TYPE_DATA && p->subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) || (p->address < 0x812000 && end > 0x10000)))) {
            esp_partition_iterator_release(it); return ESP_ERR_NOT_SUPPORTED;
        }
        it = esp_partition_next(it);
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (!run || !boot || (run != a && run != b) || boot->address != run->address) return ESP_ERR_INVALID_STATE;
    if (memcmp(esp_app_get_description()->project_name, "coffee_watch_demo", sizeof("coffee_watch_demo"))) return ESP_ERR_NOT_SUPPORTED;
    if (require_valid) {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(run, &state) != ESP_OK || state != ESP_OTA_IMG_VALID) return ESP_ERR_INVALID_STATE;
    }
    if (inactive) *inactive = run == a ? b : a;
    return ESP_OK;
}
static esp_err_t transport_supported(void)
{
#if !defined(CONFIG_MBEDTLS_HAVE_TIME_DATE) || !defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) || !defined(CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC) || !defined(CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP) || (!defined(CONFIG_LOG_DYNAMIC_LEVEL_CONTROL) && CONFIG_LOG_MAXIMUM_LEVEL >= 4)
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* PSRAM is required, never spill OTA buffers into scarce internal RAM. */
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
#endif
}
/* Worker only: wifi setup initialized this shared service with start=false.
 * No Dropbox credentials are needed. Start once per cold-clock attempt, never
 * stop/deinit the shared service. Clock + manifest share the existing 30s
 * deadline; the clock phase additionally has a 15s/15-wait bound. */
static esp_err_t clock_ready(response_t *r)
{
    if (stopped(r)) return ESP_ERR_TIMEOUT;
    if (time(NULL) > 1735689600) return ESP_OK;
    esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK) return err;
    int64_t deadline = esp_timer_get_time() + 15000000LL;
    if (deadline > r->deadline) deadline = r->deadline;
    for (unsigned slice = 0; slice < 15; ++slice) {
        if (stopped(r)) return ESP_ERR_TIMEOUT;
        int64_t remaining = deadline - esp_timer_get_time();
        if (remaining < 1000) return ESP_ERR_TIMEOUT;
        unsigned ms = remaining < 1000000 ? (unsigned)(remaining / 1000) : 1000;
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(ms));
        if (stopped(r) || esp_timer_get_time() > deadline) return ESP_ERR_TIMEOUT;
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return err;
        /* The semaphore can be stale or consumed by another shared user;
         * actual wall time, not the notification alone, is authoritative. */
        if (time(NULL) > 1735689600) return ESP_OK;
        if (err == ESP_OK) return ESP_ERR_INVALID_STATE;
    }
    return ESP_ERR_TIMEOUT;
}
static esp_err_t header_event(esp_http_client_event_t *e)
{
    response_t *r = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    if (!strcasecmp(e->header_key, "Location")) {
        size_t n = strnlen(e->header_value, URL_CAP);
        if (r->location_seen || n == 0 || n >= URL_CAP) r->bad = true;
        else memcpy(r->location, e->header_value, n + 1);
        r->location_seen = true;
    } else if (!strcasecmp(e->header_key, "Content-Length")) {
        uint32_t n = 0;
        size_t len = strnlen(e->header_value, 32);
        if (r->length_seen || len == 32 || um_content_length(e->header_value, len, &n) != 0) r->bad = true;
        r->length_seen = true; r->length = n;
    } else if (!strcasecmp(e->header_key, "Content-Encoding")) {
        if (r->encoding_seen || strcasecmp(e->header_value, "identity")) r->bad = true;
        r->encoding_seen = true;
    } else if (!strcasecmp(e->header_key, "Transfer-Encoding")) {
        /* No chunked/ambiguous framing; release assets supply Content-Length. */
        r->bad = true;
    }
    return ESP_OK;
}
/* Every hop is a fresh client with no cookies, authorization or automatic
 * redirects. Never print URLs: asset query strings can contain bearer data. */
static esp_err_t open_public(const char *initial, response_t *r, esp_http_client_handle_t *out)
{
    char *url = heap_caps_malloc(URL_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *next = heap_caps_malloc(URL_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url || !next) { free(url); free(next); return ESP_ERR_NO_MEM; }
    esp_err_t err = ESP_FAIL;
    if (um_resolve_url(initial, initial, url, URL_CAP) != 0) goto done;
    for (unsigned hop = 0; hop <= 5; ++hop) {
        if (stopped(r)) { err = ESP_ERR_TIMEOUT; break; }
        int64_t deadline = r->deadline;
        memset(r, 0, sizeof(*r)); r->deadline = deadline;
        esp_http_client_config_t cfg = {
            .url = url, .timeout_ms = 8000, .disable_auto_redirect = true,
            .buffer_size = 1024, .buffer_size_tx = 1024,
            .event_handler = header_event, .user_data = r,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
        };
#ifndef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        err = ESP_ERR_NOT_SUPPORTED; break;
#endif
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) { err = ESP_ERR_NO_MEM; break; }
        esp_http_client_set_header(c, "Accept-Encoding", "identity");
        esp_http_client_set_header(c, "User-Agent", "coffee-watch-ota/1");
        err = esp_http_client_open(c, 0);
        int64_t length = err == ESP_OK ? esp_http_client_fetch_headers(c) : -1;
        int status = esp_http_client_get_status_code(c);
        if (length < 0 || r->bad || stopped(r)) err = ESP_FAIL;
        else if (status == 200 && r->length_seen && length == (int64_t)r->length && r->length > 0 && !esp_http_client_is_chunked_response(c)) {
            *out = c; err = ESP_OK; goto done;
        } else if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && hop < 5 && r->location_seen && um_resolve_url(url, r->location, next, URL_CAP) == 0) {
            memcpy(url, next, strlen(next) + 1); esp_http_client_cleanup(c); continue;
        } else err = ESP_FAIL;
        esp_http_client_cleanup(c); break;
    }
done:
    free(url); free(next); return err;
}
static esp_err_t manifest_fetch(um_manifest_t *m, response_t *r)
{
    esp_http_client_handle_t c = NULL;
    esp_err_t err = open_public(LATEST, r, &c);
    if (err != ESP_OK) return err;
    char *body = heap_caps_malloc(CHUNK + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) { esp_http_client_cleanup(c); return ESP_ERR_NO_MEM; }
    size_t n = 0;
    if (r->length > CHUNK) { err = ESP_ERR_INVALID_SIZE; goto done; }
    while (n < r->length) {
        if (stopped(r)) { err = ESP_ERR_TIMEOUT; goto done; }
        int got = esp_http_client_read(c, body + n, (int)(r->length - n));
        if (got <= 0) { err = ESP_FAIL; goto done; }
        n += got;
    }
    if (!esp_http_client_is_complete_data_received(c) || um_parse(body, n, m) != 0) err = ESP_ERR_INVALID_RESPONSE;
    else err = ESP_OK;
done:
    free(body); esp_http_client_cleanup(c); return err;
}
static esp_err_t check_image_prefix(const unsigned char *buf, size_t n, const um_manifest_t *m)
{
    const size_t offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (n < offset + sizeof(esp_app_desc_t)) return ESP_ERR_INVALID_SIZE;
    esp_image_header_t h; esp_app_desc_t desc;
    memcpy(&h, buf, sizeof(h)); memcpy(&desc, buf + offset, sizeof(desc));
    const esp_app_desc_t *running = esp_app_get_description();
    if (h.magic != ESP_IMAGE_HEADER_MAGIC || h.chip_id != ESP_CHIP_ID_ESP32S3 || h.spi_size != ESP_IMAGE_FLASH_SIZE_16MB || desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
        memcmp(desc.project_name, "coffee_watch_demo", sizeof("coffee_watch_demo")) ||
        !memchr(desc.version, 0, sizeof(desc.version)) || strcmp(desc.version, m->version) || desc.secure_version > running->secure_version) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}
static esp_err_t download_image(const um_manifest_t *m, const esp_partition_t *slot, response_t *r)
{
    char *url = heap_caps_malloc(URL_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    unsigned char *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url || !buf) { free(url); free(buf); return ESP_ERR_NO_MEM; }
    esp_http_client_handle_t c = NULL; esp_ota_handle_t ota = 0; bool begun = false;
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    esp_err_t err = ESP_FAIL;
    if (um_firmware_url(m, url, URL_CAP) != 0) goto done;
    err = open_public(url, r, &c);
    if (err != ESP_OK) goto done;
    if (r->length != m->size || m->size > slot->size) { err = ESP_ERR_INVALID_SIZE; goto done; }
    if (mbedtls_sha256_starts(&sha, 0)) { err = ESP_FAIL; goto done; }
    uint32_t n = 0; size_t prefix = 0;
    /* Assemble prefix across arbitrarily short reads before any flash erase. */
    const size_t needed = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (m->size < needed) { err = ESP_ERR_INVALID_SIZE; goto done; }
    while (n < m->size) {
        if (stopped(r)) { err = ESP_ERR_TIMEOUT; goto done; }
        size_t want = begun ? CHUNK : needed - prefix;
        if (want > m->size - n) want = m->size - n;
        int got = esp_http_client_read(c, (char *)buf + (begun ? 0 : prefix), want);
        if (got <= 0) { err = ESP_FAIL; goto done; }
        if (!begun) {
            prefix += got; n += got;
            if (prefix < needed) continue;
            err = check_image_prefix(buf, prefix, m);
            if (err != ESP_OK) goto done;
            if (stopped(r)) { err = ESP_ERR_TIMEOUT; goto done; }
            err = esp_ota_begin(slot, m->size, &ota);
            /* IDF 5.5 sets the handle before erase; erase failure still needs
             * abort to free the allocated OTA entry. */
            begun = ota != 0;
            if (err != ESP_OK) goto done;
            got = (int)prefix;
        } else n += got;
        if (mbedtls_sha256_update(&sha, buf, got)) { err = ESP_FAIL; goto done; }
        err = esp_ota_write(ota, buf, got);
        if (err != ESP_OK) goto done;
        progress(n);
    }
    unsigned char digest[32];
    if (!esp_http_client_is_complete_data_received(c) || mbedtls_sha256_finish(&sha, digest)) { err = ESP_FAIL; goto done; }
    unsigned diff = 0;
    for (size_t i = 0; i < sizeof(digest); ++i) diff |= digest[i] ^ m->sha256[i];
    if (diff) { err = ESP_ERR_INVALID_CRC; goto done; }
    if (stopped(r)) { err = ESP_ERR_TIMEOUT; goto done; }
    err = esp_ota_end(ota); begun = false; /* end consumes handle on failure too */
    if (err == ESP_OK) {
        esp_app_desc_t desc;
        err = esp_ota_get_partition_description(slot, &desc);
        if (err == ESP_OK && (memcmp(desc.project_name, "coffee_watch_demo", sizeof("coffee_watch_demo")) || !memchr(desc.version, 0, sizeof(desc.version)) || strcmp(desc.version, m->version))) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && stopped(r)) err = ESP_ERR_TIMEOUT;
done:
    if (begun) esp_ota_abort(ota);
    if (c) esp_http_client_cleanup(c);
    mbedtls_sha256_free(&sha); free(url); free(buf); return err;
}
static void worker(void *unused)
{
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        LOCK(); int command = s_command; UNLOCK();
        /* IDF HTTP_CLIENT DEBUG prints Location and request headers (including
         * release-asset query signatures). Silence the tag for this operation,
         * restoring the caller's setting afterwards. Parent serializes network. */
        esp_log_level_t previous_log_level = esp_log_level_get("HTTP_CLIENT");
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
        const esp_partition_t *slot = NULL;
        esp_err_t err = layout(&slot, true);
        if (err == ESP_OK) err = transport_supported();
        um_manifest_t m = {0};
        response_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!r) err = ESP_ERR_NO_MEM;
        if (err == ESP_OK) {
            r->deadline = esp_timer_get_time() + 30000000LL;
            err = clock_ready(r);
            if (err == ESP_OK) err = manifest_fetch(&m, r);
            if (err == ESP_OK && stopped(r)) err = ESP_ERR_TIMEOUT;
        }
        uint32_t current = 0; int development = 0;
        const esp_app_desc_t *running_desc = esp_app_get_description();
        if (err == ESP_OK && (!memchr(running_desc->version, 0, sizeof(running_desc->version)) || um_running_version(running_desc->version, &current, &development) != 0)) err = ESP_ERR_INVALID_VERSION;
        bool newer = err == ESP_OK && (development || m.build > current);
        if (err == ESP_OK && command == 2 && !newer) err = ESP_ERR_INVALID_VERSION;
        if (err == ESP_OK) {
            LOCK(); snprintf(s_status.version, sizeof(s_status.version), "%s", m.version); s_status.total_bytes = m.size;
            if (command == 2) { s_status.state = FIRMWARE_UPDATE_DOWNLOADING; snprintf(s_status.message, sizeof(s_status.message), "Downloading inactive slot; boot selection unchanged"); }
            UNLOCK();
            if (command == 2) { r->deadline = esp_timer_get_time() + 300000000LL; err = download_image(&m, slot, r); }
        }
        free(r);
        esp_log_level_set("HTTP_CLIENT", previous_log_level);
        LOCK();
        if (s_cancel) { s_status.state = FIRMWARE_UPDATE_CANCELLED; s_status.error = ESP_ERR_INVALID_STATE; snprintf(s_status.message, sizeof(s_status.message), "Cancelled; boot selection unchanged"); }
        else if (err != ESP_OK) { s_status.state = FIRMWARE_UPDATE_ERROR; s_status.error = err; snprintf(s_status.message, sizeof(s_status.message), "Update refused or failed; boot selection unchanged"); }
        else if (command == 2) { s_ready = slot; s_manifest = m; s_status.state = FIRMWARE_UPDATE_READY; snprintf(s_status.message, sizeof(s_status.message), "Ready; HTTPS + hash, not signed authenticity"); }
        else { s_status.state = newer ? FIRMWARE_UPDATE_AVAILABLE : FIRMWARE_UPDATE_CURRENT; snprintf(s_status.message, sizeof(s_status.message), "%s", newer ? "Update available; HTTPS + hash only" : "No newer update"); }
        s_status.busy = false; UNLOCK();
    }
}
esp_err_t firmware_update_init(void)
{
    /* Startup-only, serialized initialization as documented in the header. */
    if (s_worker) return ESP_OK;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
#if CONFIG_CW_LAUNCHER_APP
    /* Updates are installed by esp-launcher through the hub; this app never writes slots. */
    s_status.state = FIRMWARE_UPDATE_ERROR; s_status.error = ESP_ERR_NOT_SUPPORTED;
    snprintf(s_status.message, sizeof(s_status.message), "Updates are installed by esp-launcher");
    return ESP_OK;
#endif
    esp_err_t supported = layout(NULL, false);
    if (supported == ESP_OK) supported = transport_supported();
    if (supported != ESP_OK) {
        s_status.state = FIRMWARE_UPDATE_ERROR; s_status.error = supported;
        snprintf(s_status.message, sizeof(s_status.message), "OTA unavailable; USB schema2 migration/configuration required");
    }
    /* Permanent 8192-byte INTERNAL stack: flash writes must not run on an
     * external stack. Do not shrink without hardware high-water measurement.
     * URL/response/body/stream buffers live only in PSRAM; cJSON's small
     * transient allocations and task/HTTP control structures remain internal. */
    if (xTaskCreate(worker, "fw_update", 8192, NULL, 4, &s_worker) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
static esp_err_t request(int command)
{
    if (!s_worker) return ESP_ERR_INVALID_STATE;
    LOCK();
    if (s_status.busy) { UNLOCK(); return ESP_ERR_INVALID_STATE; }
    memset(&s_status, 0, sizeof(s_status)); s_ready = NULL; s_cancel = false;
    esp_err_t supported = layout(NULL, true);
    if (supported == ESP_OK) supported = transport_supported();
    if (supported != ESP_OK) {
        s_status.state = FIRMWARE_UPDATE_ERROR; s_status.error = supported;
        snprintf(s_status.message, sizeof(s_status.message), "OTA unavailable; USB migration/config or healthy boot required");
        UNLOCK(); return supported;
    }
    s_command = command;
    s_status.busy = true; s_status.state = FIRMWARE_UPDATE_CHECKING;
    snprintf(s_status.message, sizeof(s_status.message), "Checking public release");
    UNLOCK(); xTaskNotifyGive(s_worker); return ESP_OK;
}
esp_err_t firmware_update_request_check(void) { return request(1); }
esp_err_t firmware_update_request_download(void) { return request(2); }
void firmware_update_cancel(void)
{
    if (!s_worker) return;
    LOCK();
    s_cancel = true; s_ready = NULL;
    if (!s_status.busy && s_status.state == FIRMWARE_UPDATE_READY) { s_status.state = FIRMWARE_UPDATE_CANCELLED; snprintf(s_status.message, sizeof(s_status.message), "Cancelled; boot selection unchanged"); }
    UNLOCK();
}
esp_err_t firmware_update_get_status(firmware_update_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_worker) return ESP_ERR_INVALID_STATE;
    LOCK(); *out = s_status; UNLOCK(); return ESP_OK;
}
esp_err_t firmware_update_activate_and_restart(bool lock_owned)
{
    if (!lock_owned || !s_worker) return ESP_ERR_INVALID_STATE;
    LOCK();
    const esp_partition_t *slot = NULL;
    esp_err_t err = layout(&slot, true);
    if (err != ESP_OK || s_status.busy || s_cancel || !s_ready || slot != s_ready || s_status.state != FIRMWARE_UPDATE_READY) { UNLOCK(); return ESP_ERR_INVALID_STATE; }
    /* Rehash the exact downloaded byte range from flash before activation;
     * never trust a stale READY descriptor if another writer changed flash. */
    unsigned char *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), digest[32];
    if (!buf) { UNLOCK(); return ESP_ERR_NO_MEM; }
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    int hash_err = mbedtls_sha256_starts(&sha, 0);
    for (uint32_t offset = 0; !hash_err && offset < s_manifest.size;) {
        size_t count = s_manifest.size - offset; if (count > CHUNK) count = CHUNK;
        err = esp_partition_read(slot, offset, buf, count);
        if (err != ESP_OK) { hash_err = -1; break; }
        if (!offset) { err = check_image_prefix(buf, count, &s_manifest); if (err != ESP_OK) { hash_err = -1; break; } }
        hash_err = mbedtls_sha256_update(&sha, buf, count); offset += count;
        /* Avoid monopolizing a core/idle watchdog during the flash rehash.
         * Main-task heartbeat deadline still requires hardware verification. */
        if ((offset & 0xffffu) == 0) vTaskDelay(1);
    }
    if (!hash_err) hash_err = mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha); free(buf);
    unsigned diff = 0; if (!hash_err) for (size_t i = 0; i < 32; ++i) diff |= digest[i] ^ s_manifest.sha256[i];
    if (hash_err || diff) { s_ready = NULL; s_status.state = FIRMWARE_UPDATE_ERROR; s_status.error = ESP_ERR_INVALID_CRC; snprintf(s_status.message, sizeof(s_status.message), "Activation refused; image changed"); UNLOCK(); return ESP_ERR_INVALID_CRC; }
    err = esp_ota_set_boot_partition(slot); /* IDF validates full ESP image again. */
    if (err != ESP_OK) { UNLOCK(); return err; }
    s_status.state = FIRMWARE_UPDATE_ACTIVATING; s_status.busy = true;
    /* Keep mutex held: no cancellation/request can interleave boot selection. */
    esp_restart();
    return ESP_FAIL;
}
esp_err_t firmware_update_mark_running_valid_after_health(bool ui, bool nvs)
{
#if CONFIG_CW_LAUNCHER_APP
    /* No updater worker in the launcher variant; the same local health gate (rendered UI, NVS,
     * ten seconds of main-loop heartbeats) marks the launcher-installed image valid. */
    if (!ui || !nvs) return ESP_ERR_INVALID_STATE;
    esp_ota_img_states_t st;
    esp_err_t e = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
    if (e == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) e = esp_ota_mark_app_valid_cancel_rollback();
    return e;
#endif
    if (!ui || !nvs || !s_worker) return ESP_ERR_INVALID_STATE;
    LOCK();
    esp_err_t err = s_status.busy ? ESP_ERR_INVALID_STATE : layout(NULL, false);
    if (err == ESP_OK) {
        esp_ota_img_states_t state;
        err = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
        if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) err = esp_ota_mark_app_valid_cancel_rollback();
        else if (err == ESP_OK && state != ESP_OTA_IMG_VALID) err = ESP_ERR_INVALID_STATE;
    }
    UNLOCK(); return err;
}
