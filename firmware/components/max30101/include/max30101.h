// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// MAX30101 register protocol (SparkFun SEN-16474), independent of the I2C driver.
#define MAX30101_ADDRESS 0x57
#define MAX30101_PART_ID 0x15
#define MAX30101_REG_FIFO_WR 0x04
#define MAX30101_REG_OVF 0x05
#define MAX30101_REG_FIFO_RD 0x06
#define MAX30101_REG_FIFO_DATA 0x07
#define MAX30101_REG_FIFO_CONFIG 0x08
#define MAX30101_REG_MODE 0x09
#define MAX30101_REG_SPO2 0x0A
#define MAX30101_REG_LED_RED 0x0C
#define MAX30101_REG_LED_IR 0x0D
#define MAX30101_REG_LED_GREEN 0x0E
#define MAX30101_REG_LED_PILOT 0x10
#define MAX30101_REG_SLOTS 0x11
#define MAX30101_REG_SLOTS2 0x12
#define MAX30101_REG_PART_ID 0xFF
#define MAX30101_FIFO_DEPTH 32
// SpO2 mode stores red then IR, 3 bytes each, as 18-bit values.
#define MAX30101_SAMPLE_BYTES 6
typedef struct {
    // Each returns true on success.
    bool (*write)(void *ctx,uint8_t reg,uint8_t value);
    bool (*read)(void *ctx,uint8_t reg,uint8_t *data,size_t n);
    void (*delay_ms)(void *ctx,unsigned ms);
    void *ctx;
} max30101_bus_t;
// Part ID 0x15 (shared with MAX30102/30105).
bool max30101_identify(const max30101_bus_t *bus);
// Soft reset, then the Roast Meter configuration: 4-sample averaging with FIFO
// rollover, SpO2 mode, 16384 nA range, 50 samples/s, 411 us pulses; red, green and
// pilot LEDs off, IR at ir_pa. Clears the FIFO.
bool max30101_configure(const max30101_bus_t *bus,uint8_t ir_pa);
// Changes the IR amplitude and clears the FIFO, so later samples use it.
bool max30101_set_ir(const max30101_bus_t *bus,uint8_t ir_pa);
// Reads up to max samples of IR (oldest first). Returns the count, or -1 on a bus error.
int max30101_read_ir(const max30101_bus_t *bus,uint32_t *ir,int max);
#ifdef __cplusplus
}
#endif
