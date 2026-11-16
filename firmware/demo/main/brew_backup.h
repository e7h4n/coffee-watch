// SPDX-License-Identifier: MIT
#pragma once
#include "brew_record.h"
#ifdef __cplusplus
extern "C" {
#endif

// Summary-only, newest eight distinct IDs, in namespace cw_brew_backup of the
// existing default NVS partition. Caller initializes NVS and serializes calls.
// No flash initialization/erase, clock access, network access or reboot upload.
// UTC is Unix seconds, 0 means unknown. Higher revisions replace the same ID in
// place (do not make it newest); equal/older revisions are rejected unchanged.
// Trace metadata/samples are deliberately discarded. scales=0 (legacy) means 1.
// True only after both snapshot and selection commits succeed. On failure the
// previous snapshot remains intact. A power loss/ambiguous commit can expose the
// new snapshot on restart despite false/no return; never interpret false as a
// guaranteed rollback, or true as Dropbox success. Retry/export by ID/revision.
bool brew_backup_save(const cw_brew_record_t *record, int64_t captured_utc_seconds);

// Reads NVS afresh, index 0 newest. Returns JSON length excluding NUL, or -1 for
// missing index, malformed storage, NVS error or insufficient output space.
// On failure out[0] is NUL when possible. No synthetic timestamps/upload status.
int brew_backup_json(unsigned newest_index, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
