// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Roast-color sensor (SparkFun MAX30101) on the RoastWatch adapter: back-header
// G5 = SDA, G6 = SCL, on the second I2C controller. The adapter's board carries
// its own pull-ups; the internal ones are enabled as well.
// Boot check: true when a MAX30101 answers with part ID 0x15. Otherwise the bus
// is released and G5/G6 return to their reset state.
bool color_sensor_detect(void);
// Configures the sensor and starts a task draining its FIFO every 40 ms.
void color_sensor_start(uint8_t ir_pa);
// Requests a new IR amplitude; the sensor task applies it and clears the FIFO.
void color_sensor_set_ir(uint8_t ir_pa);
// One IR sample (12.5 Hz), tagged with the amplitude it was measured at.
typedef struct {uint32_t raw;uint8_t pa;int64_t us;} color_sample_t;
bool color_sensor_next(color_sample_t *out);
// Consecutive failed bus reads; resets on the next good read.
unsigned color_sensor_errors(void);
#ifdef __cplusplus
}
#endif
