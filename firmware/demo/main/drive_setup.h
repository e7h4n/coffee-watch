// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "drive_packet.h"
#include "brew_record.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {CW_RECORD_SAVING,CW_RECORD_SAVED,CW_RECORD_FAILED} cw_record_sync_t;
typedef struct {cw_record_sync_t state;char id[64],file[65],error[40];unsigned revision;} cw_record_status_t;
void drive_setup_init(void);
bool drive_setup_record(const cw_brew_record_t *record);
void demo_record_status(const cw_record_status_t *status);
bool drive_setup_submit(const cw_drive_command_t *command);
// Called only from the NimBLE host task; keeps one snapshot for Read Blob.
int drive_setup_status(char *json,int capacity);
// Publishes only state transitions; no token or client credential is exposed.
void wifi_setup_drive_changed(const char *state);
#ifdef __cplusplus
}
#endif
