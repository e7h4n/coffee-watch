// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CW_WIFI_SERVICE_UUID "c9a81001-9a9d-4f55-a621-5b4e7699b647"
#define CW_WIFI_CONFIG_UUID  "c9a81002-9a9d-4f55-a621-5b4e7699b647"
#define CW_WIFI_STATUS_UUID  "c9a81003-9a9d-4f55-a621-5b4e7699b647"
typedef enum {CW_NET_UNCONFIGURED,CW_NET_CONNECTING,CW_NET_CONNECTED,CW_NET_FAILED,CW_NET_DISCONNECTED} cw_net_state_t;
typedef struct {
    cw_net_state_t state;
    bool setup_active,paired,saved;
    unsigned passkey;uint32_t request_id;
    int reason,setup_error;
    char ssid[33],ip[16],drive_state[24];
} cw_network_view_t;
void wifi_setup_init(void);
void wifi_setup_register(void);
void wifi_setup_reset(int reason);
void wifi_setup_synced(uint8_t own_address_type);
void wifi_setup_open(void);
void wifi_setup_close(void);
#ifdef __cplusplus
}
#endif
