// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Brew trace: 10 Hz samples of scale weight and bed-probe temperature, recorded
// from Ready until the brew stops, then written once as CSV to Dropbox.
#define CW_TRACE_RATE_HZ 10
#define CW_TRACE_PERIOD_US (1000000/CW_TRACE_RATE_HZ)
// 20 minutes at 10 Hz; recording stops appending (truncated) when full.
#define CW_TRACE_CAPACITY 12000
// Before the pour the trace keeps a rolling 10-20 s pre-roll, so a long preparation
// (Ready arms itself) never fills it.
#define CW_TRACE_PREROLL 100
#define CW_TRACE_NO_WEIGHT INT32_MIN
#define CW_TRACE_NO_PROBE INT16_MIN
// Bed-probe adapter: 3V3 -R1 10k 0.1%- G8 -NTC- GND, and G9 = 3V3/2 (2 x 10k 0.1%).
// The G8/G9 ratio cancels supply and ADC-reference drift.
#define CW_PROBE_R_PULLUP 10000.0
#define CW_PROBE_R25 100000.0
#define CW_PROBE_BETA 3950.0
typedef struct {uint32_t t_ms;int32_t weight_cg;int16_t probe_cc;} cw_trace_sample_t;
// Immutable copy of a finished trace, handed from the recorder to the Drive worker.
// One allocation: samples points just past the struct, so free() releases both.
typedef struct {size_t count;uint32_t brew_ms;bool truncated;cw_trace_sample_t *samples;} cw_trace_snapshot_t;
typedef struct {
    cw_trace_sample_t *samples;size_t capacity,count;
    bool active,brew_started,truncated;
    int64_t start_us;uint32_t brew_ms;
    // Samples keep the scale reading as received; the chart and the snapshot report
    // weight relative to the brew's zero (the stable weight before the pour).
    int32_t zero_cg;
} cw_trace_t;
// Probe temperature from the G8/G9 voltage ratio; NAN when open, shorted or out of range.
double cw_probe_celsius(double ratio);
// Centi-degrees with half-away rounding, or CW_TRACE_NO_PROBE when invalid.
int16_t cw_probe_cc(double celsius);
void cw_trace_init(cw_trace_t *t,cw_trace_sample_t *storage,size_t capacity);
// Start clears the previous trace. Brew marks the detected pour onset (clamped to
// the trace start); stop freezes it.
void cw_trace_start(cw_trace_t *t,int64_t us);
void cw_trace_brew(cw_trace_t *t,int64_t us,int32_t zero_cg);
void cw_trace_stop(cw_trace_t *t);
bool cw_trace_append(cw_trace_t *t,int64_t us,int32_t weight_cg,int16_t probe_cc);
// CSV with t_ms relative to the brew start (negative before the pour), weight in
// grams with one decimal and probe temperature in degrees C with two decimals.
// Missing values are empty fields. Returns the length, or -1 if it does not fit.
int cw_trace_csv(const cw_trace_sample_t *samples,size_t count,uint32_t brew_ms,char *out,size_t capacity);
size_t cw_trace_csv_bound(size_t count);
// Brew-screen chart. The time axis shows 2:30, then 4:00, 6:00 and further
// whole two-minute steps, so the curve compresses instead of scrolling.
uint32_t cw_trace_span_ms(uint32_t elapsed_ms);
// One value per chart column: the last sample in each equal bucket of [0,span)
// after the brew start. Columns without a sample stay missing. Returns the
// number of columns up to the newest sample (0 before the brew start).
size_t cw_trace_series(const cw_trace_t *t,uint32_t span_ms,size_t columns,int16_t *probe_cc,int32_t *weight_cg);
// Milliseconds from the brew start to the newest sample, or 0.
uint32_t cw_trace_elapsed_ms(const cw_trace_t *t);
