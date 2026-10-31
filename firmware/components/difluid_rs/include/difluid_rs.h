// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RS_FRAME_CAPACITY 512

typedef enum { RS_MEASUREMENT, RS_MEASUREMENT_ERROR, RS_FRAME_REJECTED,
               RS_ROUTE_UNSUPPORTED } rs_event_kind_t;
typedef enum { RS_OK, RS_BOUNDARY, RS_ESCAPE, RS_OVERFLOW, RS_CHECKSUM,
               RS_LEVEL, RS_CIPHER, RS_LENGTH, RS_CRC, RS_NONFINITE } rs_error_t;
typedef struct {
    rs_event_kind_t kind;
    rs_error_t error;
    uint8_t error_code, count;
    uint32_t error_flags;
    float tds_percent, brix_avg, temperature_c, refraction;
    float x_pos_glue, x_pos_liquid;
    uint16_t extra_bytes;
} rs_event_t;
// Decrypt exactly n bytes of AES-128-CBC level-1 RS data, without unpadding.
// The transport owns the key/IV; no key discovery or alternate cipher path.
typedef bool (*rs_decrypt_fn)(void *context, const uint8_t *cipher,
                              uint8_t *plain, size_t n);
typedef void (*rs_emit_fn)(void *context, const rs_event_t *event);
typedef struct {
    uint8_t state;
    size_t length;
    uint8_t body[RS_FRAME_CAPACITY];
    uint8_t plain[RS_FRAME_CAPACITY];
} rs_stream_t;
void rs_stream_reset(rs_stream_t *stream);
void rs_stream_feed(rs_stream_t *stream, const uint8_t *bytes, size_t n,
                    rs_decrypt_fn decrypt, void *decrypt_context,
                    rs_emit_fn emit, void *emit_context);
