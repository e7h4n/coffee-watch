// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Roast color from the IR reflectance of ground coffee behind the measuring glass,
// read by a MAX30101 exactly as the open-source Roast Meter (juztins-lab/roast-meter)
// does: SpO2 mode with the red LED at 0 mA, 50 samples/s averaged by 4, 411 us pulses
// (18 bit) and the 16384 nA range. The index is the Roast Meter's community fit to a
// Lighttells CM-100, x - (117 - x) * 0.165 with x = IR / 1000. It ranks one device's
// samples; it is not a commercial Agtron value.
#define RC_RAW_MAX 262143u
// Readings at the 18-bit ceiling are not a color.
#define RC_SATURATED_RAW 260000u
// Roast Meter's "unblocked" level: below it nothing lies on the glass. The lower
// unload level keeps a reading near the boundary from flickering.
#define RC_LOAD_RAW 30000u
#define RC_UNLOAD_RAW 28000u
// IR LED (LED2) amplitude, 0.2 mA per step; the Roast Meter default before calibration.
#define RC_DEFAULT_PA 135
// The Roast Meter warms its LED for 60 s before the first measurement.
#define RC_WARMUP_US 60000000LL
// A reading is the mean of 2 s of samples whose spread stays within 0.3 % of it.
// A gap over 0.5 s restarts the window.
#define RC_WINDOW_US 2000000LL
#define RC_GAP_US 500000LL
#define RC_SWING 0.003f
// Three readings, lifting and placing the housing between them, make a result.
#define RC_GROUP 3
#define RC_RETEST_SD 1.5f
// Fresh baking soda is calibrated to this index by the IR LED amplitude alone.
#define RC_SODA_INDEX 230.f

float rc_index(float raw);
float rc_raw_for_index(float index);

// Notification-driven stability window over raw samples.
typedef struct {bool active;uint32_t min,max;uint64_t sum;unsigned n;int64_t first,last;float progress;} rc_window_t;
void rc_window_reset(rc_window_t *w);
// Adds a sample; true (with the window mean) once span_us of samples stay within swing.
bool rc_window_add(rc_window_t *w,uint32_t raw,int64_t us,float swing,int64_t span_us,float *mean);

typedef enum {RC_WARMUP,RC_PLACE,RC_MEASURING,RC_READING,RC_RESULT,RC_SATURATED} rc_phase_t;
enum {RC_EV_LOAD=1,RC_EV_UNLOAD=2,RC_EV_READING=4,RC_EV_RESULT=8};
typedef struct {
    int64_t ready_us;
    bool loaded,captured,saturated,have_raw;
    uint32_t raw;int64_t raw_us;
    rc_window_t window;
    unsigned count;float readings[RC_GROUP];
    float mean,sd;bool retest;
} rc_meter_t;
void rc_meter_init(rc_meter_t *m,int64_t start_us);
// Clears the readings; the next placement starts a new group.
void rc_meter_clear(rc_meter_t *m);
// Feeds one sample; returns RC_EV_* bits for what it changed.
unsigned rc_meter_sample(rc_meter_t *m,uint32_t raw,int64_t us);
rc_phase_t rc_meter_phase(const rc_meter_t *m,int64_t now_us);

// Baking-soda calibration: find the IR amplitude whose reading is closest to
// RC_SODA_INDEX. It probes 255 (failing if even that is too dark), bisects 1..255,
// compares the two neighbours around the target and measures the choice once more.
// Each step skips 300 ms after an amplitude change and averages the next 800 ms.
// The repeat must agree with the first measurement at that amplitude: the result is
// as close to the target as one amplitude step allows, which is coarse at low levels.
typedef enum {RC_CAL_PLACE,RC_CAL_SETTLE,RC_CAL_STEP,RC_CAL_DONE,RC_CAL_FAILED} rc_cal_phase_t;
typedef enum {RC_CAL_OK,RC_CAL_TOO_DARK,RC_CAL_MOVED} rc_cal_error_t;
typedef enum {RC_STEP_TOP,RC_STEP_BISECT,RC_STEP_BELOW,RC_STEP_VERIFY} rc_step_t;
#define RC_CAL_SKIP_US 300000LL
#define RC_CAL_AVERAGE_US 800000LL
// Within one step, and between the two measurements of the chosen amplitude, the
// readings may differ by at most this fraction (more means a lift or a slide).
#define RC_CAL_SWING 0.02f
#define RC_CAL_STEPS 11
typedef struct {
    rc_cal_phase_t phase;rc_cal_error_t error;rc_step_t step;
    uint8_t original,pa,lo,hi,chosen;
    rc_window_t window;
    bool step_started;int64_t step_us;uint64_t sum;unsigned n;uint32_t min,max;
    uint32_t measured[256];
    unsigned steps;
    float index;
} rc_cal_t;
void rc_cal_start(rc_cal_t *c,uint8_t current_pa);
// Feeds one sample taken at sample_pa. Returns true when *set_pa must be applied
// to the sensor (a new step, the chosen amplitude, or the original after a failure).
bool rc_cal_sample(rc_cal_t *c,uint32_t raw,uint8_t sample_pa,int64_t us,uint8_t *set_pa);
float rc_cal_progress(const rc_cal_t *c);
#ifdef __cplusplus
}
#endif
