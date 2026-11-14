// SPDX-License-Identifier: MIT
#pragma once
#include "bookoo.h"
#include "difluid_rs.h"
#include "pour.h"
typedef enum {CW_HOME,CW_BREWER,CW_EMPTY,CW_PREP,CW_BREW,CW_BEVERAGE,CW_TDS,CW_REPORT,CW_READY} cw_stage_t;
// Carousel order; record and UI names use the same indices.
typedef enum {CW_V60,CW_SWITCH,CW_AEROPRESS,CW_HOOP} cw_brewer_t;
typedef struct {bool active;int32_t min,max;int64_t first,last;float progress;} cw_stable_t;
// Watch-owned brew result: start, zero, water and time never come from scale Auto events.
typedef struct {uint32_t elapsed_ms;int32_t dose_cg,water_cg;bool dose_valid,water_valid;} cw_result_t;
typedef struct {
    cw_stage_t stage;
    bookoo_session_t scale;
    cw_stable_t stable;
    unsigned generation,brewer,initial_step;
    bool scale_ready,rs_ready,interrupted,write_pending,live_valid;
    int32_t live_cg,empty_cg,beverage_cg;
    bool empty_valid,beverage_valid,tds_valid;
    // Pour detection (Ready) and the brew it starts.
    cw_pour_t pour;
    int32_t zero_cg,brew_dose_cg,level_cg;
    int64_t onset_us,level_us;
    bool brew_dose_valid,level_valid,result_valid;
    cw_result_t result;
    float tds,temperature,refraction,extraction,retention,ratio;
    int64_t wait_tds_us;
    char error[64];
} cw_flow_t;
// Centigrams rounded half away from zero to the product precision, 0.1g.
int32_t cw_mass_cg(int32_t value);
// Switch (valve-closed steep, then a full drain) and AeroPress (inverted or
// outlet-closed steep, one full press, no bypass dilution) leave brew at cup
// strength in the grounds, so extraction counts all added water (immersion
// formula). V60 and Hoop use the measured beverage mass.
bool cw_brewer_immersion(unsigned brewer);
void cw_init(cw_flow_t *s);
// Home keeps the brewer carousel position for the next brew.
void cw_home(cw_flow_t *s);
void cw_scale_link(cw_flow_t *s,bool connected,unsigned epoch);
void cw_rs_link(cw_flow_t *s,bool connected);
void cw_choose(cw_flow_t *s,unsigned brewer);
void cw_scale_message(cw_flow_t *s,unsigned epoch,const bookoo_message_t *m,int64_t us);
// Bed-probe reading (10 Hz). While Ready, a temperature rise plus a weight rise
// starts the brew; the zero is the last stable weight before the pour.
void cw_probe_message(cw_flow_t *s,bool valid,float celsius,int64_t us);
void cw_rs_message(cw_flow_t *s,const rs_event_t *e,int64_t us);
// Reset is dispatch only; write completion is transport status, never a tare ACK.
// Brewer confirmation runs a fixed, single-pass mode query, then (Auto submode
// only) Exit Ready -> Exit Done, then tare. The scale must be in Ratio mode for
// its stored dose; its Auto submode is never armed and any Auto event while a
// brew is being prepared or measured interrupts it (the scale tares itself there).
bool cw_initial_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_reset_dispatch(cw_flow_t *s,uint8_t packet[6]);
void cw_write_done(cw_flow_t *s,bool success);
// There is no Start action: preparation arms pour detection (Ready) by itself
// once the empty server and the stored dose are known. No scale command is sent.
void cw_back(cw_flow_t *s);
void cw_revisit_stopped(cw_flow_t *s);
