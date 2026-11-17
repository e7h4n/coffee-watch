#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* HTTPS + SHA256 integrity only, NOT signed authenticity. No provisioned key.
 * Explicit user requests only. Init once from startup, before other API calls.
 * Requires schema2 A/B layout, verified TLS bundle and bootloader rollback.
 * Never call from an ISR. No API draws UI or requires network/BLE health. */
typedef enum {
    FIRMWARE_UPDATE_IDLE, FIRMWARE_UPDATE_CHECKING, FIRMWARE_UPDATE_AVAILABLE,
    FIRMWARE_UPDATE_CURRENT, FIRMWARE_UPDATE_DOWNLOADING, FIRMWARE_UPDATE_READY,
    FIRMWARE_UPDATE_CANCELLED, FIRMWARE_UPDATE_ERROR, FIRMWARE_UPDATE_ACTIVATING
} firmware_update_state_t;
typedef struct {
    firmware_update_state_t state;
    bool busy;
    uint32_t received_bytes, total_bytes;
    esp_err_t error;
    char version[32];
    char message[96]; /* Sanitized local text only. Never URL/header/token data. */
} firmware_update_status_t;
/* Unsupported/factory layout is a status error, not an init failure. Only
 * worker/mutex allocation can fail init; no migration or reset is performed. */
esp_err_t firmware_update_init(void);
esp_err_t firmware_update_request_check(void);
esp_err_t firmware_update_request_download(void);
/* Cooperative: discards READY immediately, otherwise keep modal/network
 * maintenance lock until status.busy is false. I/O/flash calls may delay it. */
void firmware_update_cancel(void);
esp_err_t firmware_update_get_status(firmware_update_status_t *out);
/* Caller contract: hold exclusive modal safety lock throughout this call.
 * false refuses activation; true does not prove ownership. READY only.
 * Success selects boot partition and immediately restarts, does not return. */
esp_err_t firmware_update_activate_and_restart(bool modal_safety_lock_owned);
/* Call after successful UI and NVS startup health. Verifies actual running
 * partition and pending state. VALID is idempotent; all other states refuse. */
esp_err_t firmware_update_mark_running_valid_after_health(bool ui_healthy, bool nvs_healthy);
#ifdef __cplusplus
}
#endif
