// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "dropbox_packet.h"
#include "brew_record.h"
#include "brew_trace.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {CW_RECORD_SAVING,CW_RECORD_SAVED,CW_RECORD_FAILED} cw_record_sync_t;
typedef struct {cw_record_sync_t state;char id[64],path[96],error[40];unsigned revision;} cw_record_status_t;
void dropbox_setup_init(void);
// Takes ownership of trace (may be NULL); it is uploaded only with a record's first draft.
bool dropbox_setup_record(const cw_brew_record_t *record,cw_trace_snapshot_t *trace);
void demo_record_status(const cw_record_status_t *status);
bool dropbox_setup_submit(const cw_dropbox_command_t *command);
// Called only from the NimBLE host task; keeps one snapshot for Read Blob.
int dropbox_setup_status(char *json,int capacity);
// Publishes only state transitions; no token, app secret or code is exposed.
void wifi_setup_dropbox_changed(const char *state);
#ifdef __cplusplus
}
#endif
