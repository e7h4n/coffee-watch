// SPDX-License-Identifier: MIT
#pragma once
#include "bookoo.h"
#include "difluid_rs.h"
typedef enum {CW_HOME,CW_BREWER,CW_EMPTY,CW_PREP,CW_BREW,CW_BEVERAGE,CW_TDS,CW_REPORT,CW_READY} cw_stage_t;
// Carousel order; record and UI names use the same indices.
typedef enum {CW_V60,CW_SWITCH,CW_AEROPRESS,CW_HOOP} cw_brewer_t;
typedef struct {bool active;int32_t min,max;int64_t first,last;float progress;} cw_stable_t;
typedef struct {
    cw_stage_t stage;
    bookoo_session_t scale;
    cw_stable_t stable;
    unsigned generation,brewer,initial_step,reset_step;
    bool scale_ready,rs_ready,interrupted,write_pending,live_valid,scale_done,scale_reset;
    int32_t live_cg,empty_cg,beverage_cg;
    bool empty_valid,beverage_valid,tds_valid,back_pending;
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
void cw_rs_message(cw_flow_t *s,const rs_event_t *e,int64_t us);
// Reset is dispatch only; write completion is transport status, never a tare ACK.
// Fixed, single-pass mode query -> Exit Ready -> Exit Done -> tare dispatch.
// The same sequence runs after brewer confirmation and again once the final
// beverage weight is frozen (server lifted), returning the scale to its idle,
// tared state while the result stays on the watch. A failure in that post-brew
// pass only abandons it; the result and the TDS wait are unaffected.
bool cw_initial_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_reset_dispatch(cw_flow_t *s,uint8_t packet[6]);
void cw_write_done(cw_flow_t *s,bool success);
bool cw_ready_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_exit_ready_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_timer_request(cw_flow_t *s,bookoo_command_t command,uint8_t packet[6]);
void cw_back(cw_flow_t *s);
void cw_revisit_stopped(cw_flow_t *s);
