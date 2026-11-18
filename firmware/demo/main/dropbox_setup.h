// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "dropbox_packet.h"
#include "brew_record.h"
#include "brew_trace.h"
#include "roast_color.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {CW_RECORD_SAVING,CW_RECORD_SAVED,CW_RECORD_FAILED} cw_record_sync_t;
typedef struct {cw_record_sync_t state;char id[64],path[96],error[40];unsigned revision;} cw_record_status_t;
void dropbox_setup_init(void);
// Task-only admission decision (short mutex, no worker wait/network/cancel). Begin
// succeeds only after initialization, with no queued/active work or retained
// unsaved recovery state, and no existing maintenance holder. While held, ALL
// submits fail without changing authorization/settings; record takes trace
// ownership even on rejection. The successful caller must pair begin with end.
// End only releases admission; neither operation erases settings/recovery data.
// Do not call these APIs from an ISR.
bool dropbox_setup_maintenance_begin(void);
void dropbox_setup_maintenance_end(void);
// True if uninitialized, maintenance-held, queued/active, or retained unsaved.
bool dropbox_setup_busy(void);
// Conservative pending-upload signal: true while uninitialized/queued/active
// (worker-owned recovery state cannot yet be inspected), or when an idle worker
// retains an unconfirmed brew attempt, unresolved upload hashes, or failed color
// upload. A trace cached after confirmed CSV + draft JSON does not block admission;
// every owned resumed/final attempt blocks again until its JSON is confirmed,
// even on pre-network failure with empty pending hashes. Does not count maintenance
// alone. No automatic discard is implied by either query.
bool dropbox_setup_unsaved(void);
// Takes ownership of trace (may be NULL), retaining it across failed draft/final
// uploads in one volatile worker-owned recovery slot. Pass a new snapshot for
// EVERY non-complete draft, including resumed drafts: newer same-ID snapshots
// replace retained trace and safely update the remote CSV. Pass NULL for complete.
// No reboot persistence; a
// different brew, color group or authorization change replaces this slot.
bool dropbox_setup_record(const cw_brew_record_t *record,cw_trace_snapshot_t *trace);
void demo_record_status(const cw_record_status_t *status);
// Roast-color group: /roast-color-<id>.json, rewritten whole after every reading.
// Status arrives through demo_record_status like a brew record.
bool dropbox_setup_color(const rc_record_t *record);
// op 4: asynchronous full synthetic dual-scale connection check (~2000 samples,
// CSV + draft + final) under /connection-check-* filenames, with diagnostic labels
// in JSON. Restores live brew bookkeeping and never calls demo_record_status.
// Uses existing credentials; metadata_scope_required is a safe stop, not an
// authorization change. Logs per-stage results; false means not queued.
// Serial caller may submit {.op=4}; no new hardware/UI API is needed.
bool dropbox_setup_submit(const cw_dropbox_command_t *command);
// Called only from the NimBLE host task; keeps one snapshot for Read Blob.
int dropbox_setup_status(char *json,int capacity);
// Publishes only state transitions; no token, app secret or code is exposed.
void wifi_setup_dropbox_changed(const char *state);
#ifdef __cplusplus
}
#endif
