#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Starts the esp-launcher SDK when CONFIG_CW_LAUNCHER_APP=y (no-op otherwise). Call after
 * wifi_setup_init() and transport_init(): the watch keeps owning Wi-Fi, the SDK only waits for an
 * IP address, and BLE must get its internal RAM before the SDK allocates anything. */
void cw_launcher_app_start(void);
/* Provided by main.cpp: queue one serial-console line for the main loop. */
bool cw_console_enqueue(const char *line);
#ifdef __cplusplus
}
#endif
