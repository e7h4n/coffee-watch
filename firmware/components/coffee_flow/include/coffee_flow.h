// SPDX-License-Identifier: MIT
#pragma once
#include "bookoo.h"
#include "difluid_rs.h"
typedef enum {CW_HOME,CW_BREWER,CW_EMPTY,CW_PREP,CW_BREW,CW_BEVERAGE,CW_TDS,CW_REPORT,CW_CHART,CW_READY} cw_stage_t;
typedef struct {bool active;int32_t min,max;int64_t first,last;float progress;} cw_stable_t;
typedef struct {
    cw_stage_t stage;
    bookoo_session_t scale;
    cw_stable_t stable;
    unsigned generation,brewer;
    bool scale_ready,rs_ready,interrupted,write_pending,live_valid,scale_done;
    int32_t live_cg,empty_cg,beverage_cg;
    bool empty_valid,beverage_valid,tds_valid,back_pending;
    float tds,temperature,refraction,extraction,retention,ratio;
    int64_t wait_tds_us;
    char error[64];
} cw_flow_t;
// Centigrams rounded half away from zero to the product precision, 0.1g.
int32_t cw_mass_cg(int32_t value);
void cw_init(cw_flow_t *s);
void cw_home(cw_flow_t *s);
void cw_scale_link(cw_flow_t *s,bool connected,unsigned epoch);
void cw_rs_link(cw_flow_t *s,bool connected);
void cw_choose(cw_flow_t *s,unsigned brewer);
void cw_scale_message(cw_flow_t *s,unsigned epoch,const bookoo_message_t *m,int64_t us);
void cw_rs_message(cw_flow_t *s,const rs_event_t *e,int64_t us);
// Reset is dispatch only; write completion is transport status, never a tare ACK.
bool cw_reset_dispatch(cw_flow_t *s,uint8_t packet[6]);
void cw_write_done(cw_flow_t *s,bool success);
bool cw_ready_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_exit_ready_request(cw_flow_t *s,uint8_t packet[6]);
bool cw_timer_request(cw_flow_t *s,bookoo_command_t command,uint8_t packet[6]);
void cw_back(cw_flow_t *s);
void cw_revisit_stopped(cw_flow_t *s);
