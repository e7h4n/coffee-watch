// SPDX-License-Identifier: MIT
#include "coffee_flow.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int32_t cw_mass_cg(int32_t v) {return (int32_t)((v>=0?((int64_t)v+5)/10:-((-(int64_t)v+5)/10))*10);}
bool cw_brewer_immersion(unsigned brewer) {return brewer==CW_SWITCH||brewer==CW_AEROPRESS;}
static void clear_stable(cw_flow_t *s) {memset(&s->stable,0,sizeof s->stable);}
static void change(cw_flow_t *s,cw_stage_t stage) {s->stage=stage;clear_stable(s);s->generation++;s->error[0]=0;}
void cw_init(cw_flow_t *s) {memset(s,0,sizeof *s);bookoo_session_init(&s->scale);s->stage=CW_HOME;}
void cw_home(cw_flow_t *s) {
    bool connected=s->scale_ready,rs=s->rs_ready;unsigned generation=s->generation+1,brewer=s->brewer;
    bookoo_session_t device=s->scale;
    cw_init(s);s->generation=generation;s->rs_ready=rs;s->brewer=brewer;
    if(connected) {s->scale_ready=true;s->scale=device;bookoo_session_begin(&s->scale);s->stage=CW_BREWER;}
}
void cw_scale_link(cw_flow_t *s,bool connected,unsigned epoch) {
    s->scale_ready=connected;s->live_valid=false;clear_stable(s);s->write_pending=false;
    if(connected) {bookoo_session_connect(&s->scale,epoch);if(s->stage==CW_HOME&&!s->interrupted)change(s,CW_BREWER);}
    else {bookoo_session_disconnect(&s->scale);s->interrupted=s->stage!=CW_HOME&&s->stage!=CW_BREWER&&s->stage!=CW_TDS&&s->stage!=CW_REPORT;}
}
void cw_rs_link(cw_flow_t *s,bool connected) {s->rs_ready=connected;}
void cw_choose(cw_flow_t *s,unsigned brewer) {
    if(s->stage!=CW_BREWER||!s->scale_ready)return;
    s->brewer=brewer%4;bookoo_session_begin(&s->scale);s->live_valid=false;s->initial_step=1;change(s,CW_EMPTY);
}
bool cw_initial_request(cw_flow_t *s,uint8_t packet[6]) {
    if(!packet||!s->scale_ready||s->interrupted||s->write_pending||s->stage!=CW_EMPTY)return false;
    unsigned step=s->initial_step;
    if(step!=1&&step!=3&&step!=4&&step!=5)return false;
    packet[0]=3;packet[1]=10;packet[3]=packet[4]=0;
    if(step==1)packet[2]=15; // Query current mode before issuing phase-specific writes.
    else if(step==3) {packet[2]=24;packet[3]=1;} // Auto submode: Exit Ready, never enter it.
    else if(step==4)packet[2]=7; // Auto submode: Exit Done; documented no-op in automatic Idle.
    else packet[2]=1; // Tare, never a completion ACK.
    packet[5]=packet[0]^packet[1]^packet[2]^packet[3];
    s->initial_step=step+1;s->write_pending=true;clear_stable(s);return true;
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
// A settled level that drops by at least this much is the dripper being lifted.
#define CW_LIFT_CG 3000
// Preparation arms pour detection by itself once the empty server and the scale's
// stored dose are known; there is no Start action. Earlier triggers never count, and
// the temperature baseline restarts so a probe still warm from a rinse cannot.
static void arm(cw_flow_t *s) {
    if(s->stage!=CW_PREP||!s->empty_valid||!s->scale_ready||s->interrupted||!s->scale.dose_valid)return;
    cw_pour_rearm(&s->pour);change(s,CW_READY);
}
static bool watched(cw_stage_t stage) {return stage==CW_PREP||stage==CW_READY||stage==CW_BREW||stage==CW_BEVERAGE;}
static void pour_step(cw_flow_t *s,int64_t us) {
    int32_t zero;int64_t onset;
    cw_pour_result_t r=cw_pour_check(&s->pour,us,&zero,&onset);
    if(r==CW_POUR_NO_ZERO) {s->interrupted=true;snprintf(s->error,sizeof s->error,"No stable weight before pour");return;}
    if(r!=CW_POUR_START)return;
    if(!s->empty_valid||!s->scale.dose_valid) {s->interrupted=true;snprintf(s->error,sizeof s->error,"Scale dose unavailable");return;}
    // Water may already be in: the zero is the plateau before the pour, and the
    // brew time starts where the weight left it, not at the temperature trigger.
    s->zero_cg=zero;s->onset_us=onset;s->brew_dose_cg=s->scale.dose_cg;s->brew_dose_valid=true;
    s->level_valid=false;s->result_valid=false;change(s,CW_BREW);
}
static void lift_step(cw_flow_t *s,int64_t us) {
    int32_t v=s->live_cg;
    if(!stable(s,v,us,1000000,INT32_MIN))return;
    if(!s->level_valid||v>s->level_cg-CW_LIFT_CG) {s->level_cg=v;s->level_us=us;s->level_valid=true;return;}
    // Water is the last settled level before the lift; time ends with that level.
    int32_t water=s->level_cg-s->zero_cg;
    s->result=(cw_result_t){.elapsed_ms=s->level_us>s->onset_us?(uint32_t)((s->level_us-s->onset_us)/1000):0,
        .dose_cg=s->brew_dose_cg,.water_cg=water,.dose_valid=s->brew_dose_valid&&s->brew_dose_cg>0,.water_valid=water>0};
    s->result_valid=true;
    if(s->result.water_valid&&s->result.dose_valid) {s->ratio=(float)water/s->brew_dose_cg;change(s,CW_BEVERAGE);}
    else {s->interrupted=true;snprintf(s->error,sizeof s->error,"Invalid brew result");}
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
    if(m->kind==BOOKOO_AUTO&&watched(before)) {
        // The watch never arms Auto. An Auto phase means the scale re-tares on its
        // own, which would silently move the zero under a brew being measured.
        s->interrupted=true;snprintf(s->error,sizeof s->error,"Turn off scale Auto");return;
    }
    bookoo_session_receive(&s->scale,epoch,m);
    if(m->kind==BOOKOO_DOSE&&s->stage==CW_PREP)arm(s);
    if(m->kind==BOOKOO_MODE&&s->stage==CW_EMPTY&&s->initial_step==2) {
        if(m->mode==2)s->initial_step=m->submode==2?3:5;
        else {s->interrupted=true;snprintf(s->error,sizeof s->error,"Use Ratio mode");}
    }
    if(m->kind!=BOOKOO_WEIGHT)return;
    s->live_cg=m->weight_cg;s->live_valid=true;
    if(before==CW_PREP||before==CW_READY)cw_pour_weight(&s->pour,us,s->live_cg);
    if(!s->write_pending&&!s->initial_step&&before==CW_EMPTY) {
        if(stable(s,s->live_cg,us,5000000,1000)) {s->empty_cg=s->live_cg;s->empty_valid=true;cw_pour_reset(&s->pour);change(s,CW_PREP);arm(s);}
    } else if(before==CW_READY)pour_step(s,us);
    else if(before==CW_BREW&&!s->result_valid)lift_step(s,us);
    else if(before==CW_BEVERAGE&&s->empty_valid) {
        // The server stays on the scale after the dripper is lifted.
        int32_t beverage=s->live_cg-s->empty_cg;
        if(stable(s,beverage,us,1000000,0)) {
            s->beverage_cg=beverage;s->beverage_valid=true;change(s,CW_TDS);s->wait_tds_us=us;
        }
    }
}
void cw_probe_message(cw_flow_t *s,bool valid,float celsius,int64_t us) {
    if(s->interrupted||!s->scale_ready||(s->stage!=CW_PREP&&s->stage!=CW_READY))return;
    cw_pour_probe(&s->pour,us,valid,celsius);
    if(s->stage==CW_READY)pour_step(s,us);
}
void cw_rs_message(cw_flow_t *s,const rs_event_t *e,int64_t us) {
    if(s->stage!=CW_TDS||!s->rs_ready||us<=s->wait_tds_us||!s->beverage_valid||!s->result_valid||s->interrupted)return;
    if(e->kind==RS_MEASUREMENT_ERROR) {snprintf(s->error,sizeof s->error,"Measurement error %u",e->error_code);return;}
    if(e->kind!=RS_MEASUREMENT||!isfinite(e->tds_percent)||e->tds_percent<0||e->tds_percent>=100)return;
    if(!s->result.dose_valid||s->result.dose_cg<=0)return;
    s->tds=e->tds_percent;s->temperature=e->temperature_c;s->refraction=e->refraction;
    s->extraction=cw_brewer_immersion(s->brewer)
        ?s->tds/(100-s->tds)*s->result.water_cg/s->result.dose_cg*100
        :s->tds*s->beverage_cg/s->result.dose_cg;
    s->retention=(float)(s->result.water_cg-s->beverage_cg)/s->result.dose_cg;
    s->tds_valid=true;change(s,CW_REPORT);
}
bool cw_reset_dispatch(cw_flow_t *s,uint8_t packet[6]) {
    if(!packet||!s->scale_ready||s->interrupted||s->write_pending||s->stable.active)return false;
    if(s->stage!=CW_EMPTY||s->initial_step)return false;
    packet[0]=3;packet[1]=10;packet[2]=1;packet[3]=packet[4]=0;
    packet[5]=packet[0]^packet[1]^packet[2];
    clear_stable(s);s->write_pending=true;
    return true;
}
void cw_write_done(cw_flow_t *s,bool success) {
    s->write_pending=false;clear_stable(s);
    if(success&&s->initial_step==6)s->initial_step=0;
    if(!success&&s->initial_step)s->interrupted=true;
    if(!success) {bookoo_session_write_failed(&s->scale);snprintf(s->error,sizeof s->error,"Scale write failed");}
}
void cw_revisit_stopped(cw_flow_t *s) {
    // Navigation uses the already frozen result; nothing is re-measured.
    if(s->stage==CW_BREW&&s->result_valid)change(s,CW_BEVERAGE);
}
void cw_back(cw_flow_t *s) {
    if(s->stage==CW_REPORT||s->stage==CW_BREWER||s->interrupted) {cw_home(s);return;}
    if(s->stage==CW_EMPTY) {s->initial_step=0;bookoo_session_begin(&s->scale);s->empty_valid=false;change(s,CW_BREWER);return;}
    if(s->stage==CW_PREP) {bookoo_session_begin(&s->scale);s->empty_valid=false;change(s,CW_EMPTY);return;}
    // Nothing was sent to the scale while Ready or brewing, so Back acts at once.
    // Ready re-captures the empty server; a brew (a false start from a rinse, or a
    // revisited result) is discarded and preparation re-arms immediately.
    if(s->stage==CW_READY) {bookoo_session_begin(&s->scale);s->empty_valid=false;change(s,CW_EMPTY);return;}
    if(s->stage==CW_BREW) {bookoo_session_begin(&s->scale);s->result_valid=false;change(s,CW_PREP);arm(s);return;}
    if(s->stage==CW_BEVERAGE) {s->beverage_valid=false;change(s,CW_BREW);return;}
    // The server is still on the scale, so Waiting TDS can return to re-weigh it.
    if(s->stage==CW_TDS) {if(!s->scale_ready) {cw_home(s);return;}s->beverage_valid=false;change(s,CW_BEVERAGE);}
}
