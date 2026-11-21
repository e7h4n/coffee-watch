#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* CONFIG_CW_LAUNCHER_APP=y (no-ops otherwise):
 * cw_launcher_net_start(): called by wifi_setup_init() instead of the watch's own Wi-Fi bring-up.
 *   Starts the okou_agent SDK with SDK-managed Wi-Fi (networks shared with the launcher); its large
 *   buffers live in PSRAM so BLE still gets its internal RAM.
 * cw_launcher_app_start(): after transport_init(); registers the remote commands. */
void cw_launcher_net_start(void);
void cw_launcher_app_start(void);
/* Provided by main.cpp: queue one serial-console line for the main loop. */
bool cw_console_enqueue(const char *line);
#ifdef __cplusplus
}
#endif
