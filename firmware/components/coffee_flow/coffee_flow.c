// SPDX-License-Identifier: MIT
#include "coffee_flow.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int32_t cw_mass_cg(int32_t v) {return (int32_t)((v>=0?((int64_t)v+5)/10:-((-(int64_t)v+5)/10))*10);}
static void clear_stable(cw_flow_t *s) {memset(&s->stable,0,sizeof s->stable);}
static void change(cw_flow_t *s,cw_stage_t stage) {s->stage=stage;clear_stable(s);s->generation++;s->error[0]=0;}
void cw_init(cw_flow_t *s) {memset(s,0,sizeof *s);bookoo_session_init(&s->scale);s->stage=CW_HOME;}
void cw_home(cw_flow_t *s) {
    bool connected=s->scale_ready,rs=s->rs_ready;unsigned generation=s->generation+1;
    bookoo_session_t device=s->scale;
    cw_init(s);s->generation=generation;s->rs_ready=rs;
    if(connected) {s->scale_ready=true;s->scale=device;bookoo_session_begin(&s->scale);s->stage=CW_BREWER;}
}
void cw_scale_link(cw_flow_t *s,bool connected,unsigned epoch) {
    s->scale_ready=connected;s->live_valid=false;clear_stable(s);s->write_pending=false;
    if(connected) {bookoo_session_connect(&s->scale,epoch);if(s->stage==CW_HOME&&!s->interrupted)change(s,CW_BREWER);}
    else {bookoo_session_disconnect(&s->scale);s->interrupted=s->stage!=CW_HOME&&s->stage!=CW_BREWER&&s->stage!=CW_TDS&&s->stage!=CW_REPORT&&s->stage!=CW_CHART;}
}
void cw_rs_link(cw_flow_t *s,bool connected) {s->rs_ready=connected;}
void cw_choose(cw_flow_t *s,unsigned brewer) {
    if(s->stage!=CW_BREWER||!s->scale_ready)return;
    s->brewer=brewer%4;bookoo_session_begin(&s->scale);change(s,CW_EMPTY);
}
static bool stable(cw_flow_t *s,int32_t value,int64_t us,int64_t duration,int32_t minimum) {
    cw_stable_t *w=&s->stable;
    if(value<=minimum) {clear_stable(s);return false;}
    if(!w->active||us<=w->last||us-w->last>1000000) {*w=(cw_stable_t){.active=true,.min=value,.max=value,.first=us,.last=us};return false;}
    if(value<w->min)w->min=value;
    if(value>w->max)w->max=value;
    if(w->max-w->min>20) {*w=(cw_stable_t){.active=true,.min=value,.max=value,.first=us,.last=us};return false;}
    w->last=us;w->progress=(float)(us-w->first)/(float)duration;
    return us-w->first>=duration;
}
void cw_scale_message(cw_flow_t *s,unsigned epoch,const bookoo_message_t *m,int64_t us) {
    if(!s->scale_ready||epoch!=s->scale.epoch)return;
    bookoo_message_t rounded=*m;
    rounded.weight_cg=cw_mass_cg(m->weight_cg);
    rounded.dose_cg=cw_mass_cg(m->dose_cg);
    m=&rounded;
    if(s->interrupted) {
        if(m->kind==BOOKOO_DOSE)bookoo_session_receive(&s->scale,epoch,m);
        return;
    }
    cw_stage_t before=s->stage;
    if(m->kind==BOOKOO_AUTO&&m->state==4)s->scale_done=false;
    if(m->kind==BOOKOO_AUTO&&m->state==0)s->scale_done=true;
    unsigned update=bookoo_session_receive(&s->scale,epoch,m);
    if(m->kind==BOOKOO_WEIGHT) {
        s->live_cg=m->weight_cg;s->live_valid=true;
        if(!s->write_pending&&before==CW_EMPTY) {
            if(stable(s,s->live_cg,us,5000000,1000)) {s->empty_cg=s->live_cg;s->empty_valid=true;change(s,CW_PREP);}
        } else if(before==CW_BEVERAGE&&s->empty_valid&&s->scale_done) {
            // Auto Done streams zero with the server present, negative gross
            // mass after removal. Verified on BOOKOO Ultra 4.0.1, not Stop water.
            int32_t beverage=-s->live_cg-s->empty_cg;
            if(s->live_cg>=-3000)clear_stable(s);
            else if(stable(s,beverage,us,1000000,0)) {
                s->beverage_cg=beverage;s->beverage_valid=true;
                change(s,CW_TDS);s->wait_tds_us=us;
            }
        }
    }
    if(m->kind==BOOKOO_AUTO&&m->state==2&&before==CW_PREP&&(update&BOOKOO_UPDATE_STATE)) {
        s->write_pending=false;change(s,CW_READY);
    }
    if(m->kind==BOOKOO_AUTO&&m->state==3&&before==CW_READY&&(update&BOOKOO_UPDATE_STATE)) {
        s->write_pending=false;s->back_pending=false;change(s,CW_PREP);
    }
    if(m->kind==BOOKOO_AUTO&&m->state==1&&before==CW_READY&&(update&BOOKOO_UPDATE_STATE)) {
        s->write_pending=false;
        if(s->empty_valid&&s->scale.running_dose_valid)change(s,CW_BREW);
        else {s->interrupted=true;snprintf(s->error,sizeof s->error,"Scale dose unavailable");}
    }
    if((update&BOOKOO_UPDATE_RESULT)&&before==CW_BREW) {
        s->write_pending=false;
        if(s->back_pending) {s->back_pending=false;bookoo_session_begin(&s->scale);change(s,CW_PREP);return;}
        const bookoo_result_t *r=&s->scale.result;
        if(r->water_valid&&r->dose_valid&&r->dose_cg>0) {s->ratio=(float)r->water_cg/r->dose_cg;change(s,CW_BEVERAGE);}
        else {s->interrupted=true;snprintf(s->error,sizeof s->error,"Invalid brew result");}
    }
}
void cw_rs_message(cw_flow_t *s,const rs_event_t *e,int64_t us) {
    if(s->stage!=CW_TDS||!s->rs_ready||us<=s->wait_tds_us||!s->beverage_valid||!s->scale.result_valid||s->interrupted)return;
    if(e->kind==RS_MEASUREMENT_ERROR) {snprintf(s->error,sizeof s->error,"Measurement error %u",e->error_code);return;}
    if(e->kind!=RS_MEASUREMENT||!isfinite(e->tds_percent)||e->tds_percent<0||e->tds_percent>=100)return;
    if(!s->scale.result.dose_valid||s->scale.result.dose_cg<=0)return;
    s->tds=e->tds_percent;s->temperature=e->temperature_c;s->refraction=e->refraction;
    s->extraction=s->tds*s->beverage_cg/s->scale.result.dose_cg;
    s->retention=(float)(s->scale.result.water_cg-s->beverage_cg)/s->scale.result.dose_cg;
    s->tds_valid=true;change(s,CW_REPORT);
}
bool cw_reset_dispatch(cw_flow_t *s,uint8_t packet[6]) {
    if(!packet||!s->scale_ready||s->interrupted||s->write_pending||s->scale.pending||s->stable.active)return false;
    if(s->stage!=CW_EMPTY)return false;
    // BOOKOO 4.0.1: 01 is unavailable in Auto Done; 07 explicitly exits
    // Done and tares. Select by the confirmed device phase, never by failure.
    packet[0]=3;packet[1]=10;packet[2]=s->scale_done?7:1;packet[3]=packet[4]=0;
    packet[5]=packet[0]^packet[1]^packet[2];
    clear_stable(s);s->write_pending=true;
    return true;
}
void cw_write_done(cw_flow_t *s,bool success) {
    s->write_pending=false;clear_stable(s);
    if(!success) {bookoo_session_write_failed(&s->scale);snprintf(s->error,sizeof s->error,"Scale write failed");}
}
bool cw_ready_request(cw_flow_t *s,uint8_t packet[6]) {
    if(s->stage!=CW_PREP||!s->empty_valid||!s->scale_ready||s->interrupted||s->write_pending)return false;
    if(!bookoo_session_request(&s->scale,BOOKOO_CMD_READY,packet))return false;
    s->write_pending=true;return true;
}
bool cw_exit_ready_request(cw_flow_t *s,uint8_t packet[6]) {
    if(s->stage!=CW_READY||!s->scale_ready||s->interrupted||s->write_pending)return false;
    if(!bookoo_session_request(&s->scale,BOOKOO_CMD_EXIT_READY,packet))return false;
    s->write_pending=true;return true;
}
bool cw_timer_request(cw_flow_t *s,bookoo_command_t c,uint8_t packet[6]) {
    if(!s->scale_ready||s->interrupted||s->write_pending)return false;
    if(c!=BOOKOO_CMD_STOP||s->stage!=CW_BREW)return false;
    if(!bookoo_session_request(&s->scale,c,packet))return false;
    s->write_pending=true;return true;
}
void cw_revisit_stopped(cw_flow_t *s) {
    // Navigation uses the already frozen result; no new Stop is fabricated.
    if(s->stage==CW_BREW&&s->scale.phase==BOOKOO_STOPPED&&s->scale.result_valid) {
        change(s,CW_BEVERAGE);
    }
}
void cw_back(cw_flow_t *s) {
    if(s->stage==CW_REPORT||s->stage==CW_CHART||s->stage==CW_BREWER||s->interrupted) {cw_home(s);return;}
    if(s->stage==CW_EMPTY) {bookoo_session_begin(&s->scale);s->empty_valid=false;change(s,CW_BREWER);return;}
    if(s->stage==CW_PREP) {bookoo_session_begin(&s->scale);s->empty_valid=false;change(s,CW_EMPTY);return;}
    if(s->stage==CW_READY) {s->back_pending=true;return;}
    if(s->stage==CW_BREW) {
        if(s->scale.phase==BOOKOO_RUNNING) {s->back_pending=true;return;}
        bookoo_session_begin(&s->scale);change(s,CW_PREP);return;
    }
    if(s->stage==CW_BEVERAGE) {s->beverage_valid=false;change(s,CW_BREW);return;}
    if(s->stage==CW_TDS) {if(!s->scale_ready) {cw_home(s);return;}s->beverage_valid=false;change(s,CW_BEVERAGE);}
}
