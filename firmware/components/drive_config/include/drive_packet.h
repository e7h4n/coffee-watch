// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CW_DRIVE_ID_MAX 128
#define CW_DRIVE_SECRET_MAX 96
#define CW_DRIVE_PACKET_MAX (8 + CW_DRIVE_ID_MAX + CW_DRIVE_SECRET_MAX)
typedef struct {uint8_t op;uint32_t request;char client_id[129],client_secret[97];} cw_drive_command_t;
// v1: version, op (1 authorize / 2 forget locally / 3 verify), ID length,
// secret length, nonzero LE32 request, ASCII ID and secret. Never readable.
bool cw_drive_packet_decode(const uint8_t *bytes,size_t size,cw_drive_command_t *out);

typedef enum {CW_DRIVE_POLL_FAIL,CW_DRIVE_POLL_WAIT,CW_DRIVE_POLL_SLOW,CW_DRIVE_POLL_DENIED} cw_drive_poll_action_t;
// Only Google's declared pending/slow-down replies permit another token poll.
cw_drive_poll_action_t cw_drive_poll_action(int http_status,const char *error);
