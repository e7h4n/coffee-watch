// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Lefu CK811 kitchen scale (also sold as Futula Kitchen Scale 3), 5 kg / 0.1 g.
// Layout from the open-source Pourista FutulaDriver, checked there on hardware:
// service FFF0, weight notifications on FFF4, commands on FFF1. The weight is LE16
// tenths of a gram at bytes 3..4 with the sign in byte 5 (non-zero = negative); the
// scale always sends grams whatever unit it displays (byte 8).
#define CK811_SERVICE 0xfff0
#define CK811_WEIGHT 0xfff4
#define CK811_COMMAND 0xfff1
#define CK811_FRAME_MIN 9
// Advertised names: "LEFU-CK811" or "LFSmart Scale".
bool ck811_name(const char *name,size_t length);
// Decodes one FFF4 notification into centigrams; false for a short frame.
bool ck811_weight(const uint8_t *frame,size_t length,int32_t *weight_cg);
#ifdef __cplusplus
}
#endif
