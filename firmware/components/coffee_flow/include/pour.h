// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
// Watch-owned pour detection, independent of any scale Auto mode.
// The bed probe confirms hot water; by then a few grams may already be in, so the
// zero is the last stable weight before the trigger, found in recent history.
#define CW_POUR_WEIGHTS 128
#define CW_POUR_TEMPS 64
#define CW_POUR_LOOKBACK_US 5000000   // zero must be a plateau inside the last 5 s
#define CW_POUR_PLATEAU_US 1000000    // ...lasting at least 1 s
#define CW_POUR_SPREAD_CG 20          // ...within 0.2 g, the product's stability tolerance
#define CW_POUR_GAP_US 1000000        // notification gaps >1 s break continuity
#define CW_POUR_BASELINE_US 3000000   // temperature baseline: median of the previous 3 s
#define CW_POUR_BASELINE_SKIP_US 200000
#define CW_POUR_RISE_CC 300           // trigger: 3.0 C above the baseline
#define CW_POUR_FLOOR_CC 2500         // missing readings sit below the ~25 C ADC floor
#define CW_POUR_CONFIRM_US 1500000    // weight must rise within 1.5 s of the trigger
#define CW_POUR_WATER_CG 50           // 0.5 g above zero confirms water on the scale
#define CW_POUR_ONSET_CG 30           // onset: first weight 0.3 g above zero
typedef struct {int64_t us;int32_t cg;} cw_pour_weight_t;
typedef struct {int64_t us;int16_t cc;bool valid;} cw_pour_temp_t;
typedef struct {
    cw_pour_weight_t weights[CW_POUR_WEIGHTS];unsigned weight_count,weight_next;
    cw_pour_temp_t temps[CW_POUR_TEMPS];unsigned temp_count,temp_next;
    bool triggered;int64_t trigger_us;
} cw_pour_t;
typedef enum {CW_POUR_WAIT,CW_POUR_START,CW_POUR_NO_ZERO} cw_pour_result_t;
void cw_pour_reset(cw_pour_t *p);
// Arming forgets triggers and the temperature baseline (a probe still warming from
// a rinse must not count) but keeps weight history, which the zero is found in.
void cw_pour_rearm(cw_pour_t *p);
// Product-rounded weight notification from the active connection.
void cw_pour_weight(cw_pour_t *p,int64_t us,int32_t weight_cg);
// One bed-probe reading; invalid readings are missing values, not temperatures.
// Returns true when this reading crosses the trigger threshold.
bool cw_pour_probe(cw_pour_t *p,int64_t us,bool valid,float celsius);
// Latest plateau (>=1 s, <=0.2 g, no >1 s gap) that ends inside [end-5 s, end].
// The zero is its median; plateau_end_us is its last sample.
bool cw_pour_zero(const cw_pour_t *p,int64_t end_us,int32_t *zero_cg,int64_t *plateau_end_us);
// Evaluated after every new sample while armed. START fills zero and onset; NO_ZERO
// means water arrived without a usable plateau and must not be guessed.
cw_pour_result_t cw_pour_check(cw_pour_t *p,int64_t now_us,int32_t *zero_cg,int64_t *onset_us);
