// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// All mass values are signed hundredths of a gram; no floating-point conversion.
typedef enum { BOOKOO_UNKNOWN, BOOKOO_WEIGHT, BOOKOO_DOSE, BOOKOO_AUTO, BOOKOO_MODE } bookoo_kind_t;
typedef enum { BOOKOO_PARSE_OK, BOOKOO_BAD_LENGTH, BOOKOO_BAD_CHECKSUM,
               BOOKOO_BAD_SIGN, BOOKOO_BAD_STATE } bookoo_parse_t;
typedef struct {
    bookoo_kind_t kind;
    uint8_t type, state, battery, display_unit, mode, submode;
    uint32_t elapsed_ms;
    int32_t weight_cg, flow_cg_s, dose_cg, settlement_hundredths;
} bookoo_message_t;
bookoo_parse_t bookoo_decode(const uint8_t *data, size_t size, bookoo_message_t *out);

typedef enum { BOOKOO_IDLE, BOOKOO_READY, BOOKOO_RUNNING,
               BOOKOO_STOPPED, BOOKOO_INTERRUPTED } bookoo_phase_t;
typedef enum { BOOKOO_CMD_NONE=0, BOOKOO_CMD_START=4,
               BOOKOO_CMD_STOP=5, BOOKOO_CMD_READY=0x18,
               BOOKOO_CMD_EXIT_READY=0x118 } bookoo_command_t;
typedef enum { BOOKOO_UPDATE_NONE=0, BOOKOO_UPDATE_WEIGHT=1,
               BOOKOO_UPDATE_DOSE=2, BOOKOO_UPDATE_STATE=4,
               BOOKOO_UPDATE_RESULT=8, BOOKOO_UPDATE_COMMAND=16 } bookoo_update_t;
typedef struct {
    uint32_t brew_id, elapsed_ms;
    int32_t dose_cg, water_cg;
    bool dose_valid, water_valid;
    // Water is the Stop-event settlement, validated on release 4.0.1 Ratio+Auto.
    // No streaming-weight substitute is retained in the completed result.
} bookoo_result_t;
typedef struct {
    bool connected, dose_valid, live_valid, running_dose_valid;
    uint32_t epoch, brew_id, elapsed_ms;
    bookoo_phase_t phase;
    int32_t dose_cg, live_weight_cg, running_dose_cg;
    bool result_valid;
    bookoo_result_t result;
    bookoo_command_t pending;
} bookoo_session_t;

void bookoo_session_init(bookoo_session_t *s);
// Caller owns peer identity and supplies a new generation after each connection.
void bookoo_session_connect(bookoo_session_t *s, uint32_t epoch);
void bookoo_session_disconnect(bookoo_session_t *s);
// Explicit new-brew action. Retains the scale-reported dose of this connection.
// A disconnect invalidates it until a new current-connection dose report arrives.
// Interrupted evidence is preserved until this call; reconnection alone never resumes.
void bookoo_session_begin(bookoo_session_t *s);
unsigned bookoo_session_receive(bookoo_session_t *s, uint32_t epoch,
                                const bookoo_message_t *m);
// Creates a command packet but never performs an optimistic state transition.
bool bookoo_session_request(bookoo_session_t *s, bookoo_command_t command,
                            uint8_t packet[6]);
// ATT success is not action completion. Failure clears only the pending command.
void bookoo_session_write_failed(bookoo_session_t *s);
