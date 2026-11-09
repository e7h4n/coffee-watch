// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "brew_trace.h"
#ifdef __cplusplus
extern "C" {
#endif
// Bed probe on the StopWatch back header: G8 = NTC node, G9 = 3V3/2 reference.
// A 10 Hz task reads both through ADC1 and appends weight + temperature samples
// while a trace is recording. Control calls come from the UI/flow task.
void probe_trace_init(void);
void probe_trace_start(int64_t us);
void probe_trace_brew(int64_t us);
void probe_trace_stop(void);
// Latest scale weight in centigrams (product rounding already applied).
void probe_trace_weight(bool valid,int32_t weight_cg);
// Copy of a stopped trace that reached the brew start; NULL if none or out of memory.
// The caller owns it and releases it with free().
cw_trace_snapshot_t *probe_trace_snapshot(void);
typedef struct {bool valid,active,brew_started,truncated;float celsius,ratio;int ntc_mv,ref_mv;unsigned count;} probe_reading_t;
// Brew-screen chart columns (see cw_trace_series); also reports elapsed time and axis span.
size_t probe_trace_chart(size_t columns,int16_t *probe_cc,int32_t *weight_cg,uint32_t *elapsed_ms,uint32_t *span_ms);
void probe_trace_reading(probe_reading_t *out);
#ifdef __cplusplus
}
#endif
