// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drive_packet.h"
#ifdef __cplusplus
extern "C" {
#endif
void drive_setup_init(void);
bool drive_setup_submit(const cw_drive_command_t *command);
// Called only from the NimBLE host task; keeps one snapshot for Read Blob.
int drive_setup_status(char *json,int capacity);
// Publishes only state transitions; no token or client credential is exposed.
void wifi_setup_drive_changed(const char *state);
#ifdef __cplusplus
}
#endif
