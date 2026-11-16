// SPDX-License-Identifier: MIT
#pragma once
#include "pour.h"
// Two-scale brewing in a stacked stand. Neither scale has an assigned place: the
// total scale carries everything (stand, cup scale, server, dripper, water), the cup
// scale only the server, so the total's gain since the pour is never smaller than
// the cup's. Water = total gain; beverage = cup gain; no empty server, no tare.
#define CW_DUO_SCALES 2
#define CW_DUO_HISTORY 128
#define CW_DUO_SWAP_CG 200            // the other scale gaining 2 g more...
#define CW_DUO_SWAP_US 2000000        // ...for 2 s swaps the roles
#define CW_DUO_QUIET_US 5000000       // drained: for 5 s
#define CW_DUO_POUR_QUIET_CG 50       //   water within 0.5 g (not pouring),
#define CW_DUO_DRIP_QUIET_CG 30       //   beverage within 0.3 g (not dripping),
#define CW_DUO_MIN_OUT_CG 500         //   at least 5 g of beverage and at least half
                                      //   the water (a steep or a bloom never ends it)
#define CW_DUO_FLOW_US 2000000        // out flow: beverage change over the last 2 s
#define CW_DUO_OPEN_CGPS 100          // drain start (valve open, press): out flow >= 1 g/s
#define CW_DUO_RISE_CG 10             // beverage rising: 0.1 g above its high-water mark
#define CW_DUO_RESUME_CG 300          // after the end: 3 g more water...
#define CW_DUO_RESUME_US 500000       // ...for 0.5 s resumes the brew
#define CW_DUO_FREEZE_CG 500          // after the end: 5 g less beverage (server lifted) freezes it
#define CW_DUO_LIFT_CG 3000           // a settled water level 30 g lower: dripper lifted
#define CW_DUO_SETTLE_US 1000000      // settled: 1 s within 0.2 g
#define CW_DUO_SETTLE_CG 20
typedef struct {int64_t us;int32_t cg;} cw_duo_sample_t;
typedef struct {cw_duo_sample_t s[CW_DUO_HISTORY];unsigned count,next;} cw_duo_ring_t;
typedef enum {CW_DUO_ARMED,CW_DUO_BREWING,CW_DUO_ENDED} cw_duo_phase_t;
typedef enum {CW_DUO_WAIT,CW_DUO_START,CW_DUO_NO_ZERO} cw_duo_start_t;
typedef enum {CW_DUO_NONE,CW_DUO_DRAINED,CW_DUO_LIFTED,CW_DUO_RESUMED} cw_duo_event_t;
typedef struct {
    cw_pour_t pour[CW_DUO_SCALES];
    int32_t raw[CW_DUO_SCALES];bool raw_valid[CW_DUO_SCALES];int64_t raw_us[CW_DUO_SCALES];
    cw_duo_phase_t phase;
    int32_t zero[CW_DUO_SCALES];int64_t onset_us;
    unsigned total;int64_t swap_since;
    cw_duo_ring_t gain[CW_DUO_SCALES];
    int32_t water_cg,out_cg;
    int32_t out_high;int64_t out_rise_us;
    int32_t out_cgps,peak_cgps;
    bool open_valid;int64_t open_us;
    bool level_valid;int32_t level_cg;int64_t level_us;
    bool settling;int32_t settle_min,settle_max;int64_t settle_first,settle_last;
    int64_t end_us;int32_t end_water,end_out;bool out_frozen;int64_t resume_since;
} cw_duo_t;
void cw_duo_reset(cw_duo_t *d);
// Arming forgets probe triggers and the temperature baseline, keeping weight history.
void cw_duo_rearm(cw_duo_t *d);
// Product-rounded weight from scale 0 (BOOKOO) or 1 (the second scale). A scale may
// notify only when its reading changes, so its last value holds until the next one.
void cw_duo_weight(cw_duo_t *d,unsigned scale,int64_t us,int32_t cg);
void cw_duo_probe(cw_duo_t *d,int64_t us,bool valid,float celsius);
// Armed: the probe trigger plus either scale rising 0.5 g above its own plateau starts
// the brew; the rising scale is the total scale. NO_ZERO: a scale had no plateau.
cw_duo_start_t cw_duo_check(cw_duo_t *d,int64_t now);
// Brewing or ended: evaluated after each weight. DRAINED and LIFTED end the brew;
// RESUMED returns an ended brew to brewing when water is poured again.
cw_duo_event_t cw_duo_step(cw_duo_t *d,int64_t now);
