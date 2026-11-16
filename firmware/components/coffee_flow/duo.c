// SPDX-License-Identifier: MIT
#include "duo.h"
#include <string.h>
void cw_duo_reset(cw_duo_t *d) {memset(d,0,sizeof *d);}
void cw_duo_rearm(cw_duo_t *d) {
    for(unsigned i=0;i<CW_DUO_SCALES;i++) {cw_pour_rearm(&d->pour[i]);memset(&d->gain[i],0,sizeof d->gain[i]);}
    d->phase=CW_DUO_ARMED;
}
static const cw_duo_sample_t *at(const cw_duo_ring_t *r,unsigned k) {return &r->s[(r->next+CW_DUO_HISTORY-1-k)%CW_DUO_HISTORY];}
static void push(cw_duo_ring_t *r,int64_t us,int32_t cg) {
    r->s[r->next]=(cw_duo_sample_t){us,cg};r->next=(r->next+1)%CW_DUO_HISTORY;if(r->count<CW_DUO_HISTORY)r->count++;
}
// Range over [now-span, now], including the value carried in from before the window;
// a scale that notifies only on change is steady between notifications. False until
// the history reaches back to the start of the window.
static bool range(const cw_duo_ring_t *r,int64_t now,int64_t span,int32_t *spread) {
    if(!r->count)return false;
    int32_t lo=at(r,0)->cg,hi=lo;
    for(unsigned k=0;k<r->count;k++) {
        const cw_duo_sample_t *s=at(r,k);
        if(s->cg<lo)lo=s->cg;
        if(s->cg>hi)hi=s->cg;
        if(s->us<=now-span) {*spread=hi-lo;return true;}
    }
    return false;
}
static void settle(cw_duo_t *d,int32_t v,int64_t us) {
    int32_t lo=v<d->settle_min?v:d->settle_min,hi=v>d->settle_max?v:d->settle_max;
    if(!d->settling||us-d->settle_last>CW_DUO_SETTLE_US||hi-lo>CW_DUO_SETTLE_CG) {
        d->settling=true;d->settle_min=d->settle_max=v;d->settle_first=d->settle_last=us;return;
    }
    d->settle_min=lo;d->settle_max=hi;d->settle_last=us;
}
static bool settled(const cw_duo_t *d) {return d->settling&&d->settle_last-d->settle_first>=CW_DUO_SETTLE_US;}
// The value a scale showed at time t: its last notification at or before t.
static int32_t value_at(const cw_duo_ring_t *r,int64_t t) {
    for(unsigned k=0;k<r->count;k++)if(at(r,k)->us<=t)return at(r,k)->cg;
    return r->count?at(r,r->count-1)->cg:0;
}
#define CW_DUO_HOLD_US 150000
void cw_duo_weight(cw_duo_t *d,unsigned scale,int64_t us,int32_t cg) {
    if(scale>=CW_DUO_SCALES)return;
    d->raw[scale]=cg;d->raw_valid[scale]=true;d->raw_us[scale]=us;
    if(d->phase==CW_DUO_ARMED) {
        cw_pour_weight(&d->pour[scale],us,cg);
        // A scale that notifies only on change is steady meanwhile: hold its value so a
        // still scale still shows the plateau its zero comes from.
        unsigned o=1-scale;
        if(d->raw_valid[o]&&us-d->raw_us[o]>=CW_DUO_HOLD_US)cw_pour_weight(&d->pour[o],us,d->raw[o]);
        return;
    }
    push(&d->gain[scale],us,cg-d->zero[scale]);
    unsigned cup=1-d->total;
    // Roles: the larger gain is the total scale; a sustained lead of the other swaps them.
    int32_t g_total=d->raw[d->total]-d->zero[d->total],g_cup=d->raw[cup]-d->zero[cup];
    if(g_cup>g_total+CW_DUO_SWAP_CG) {
        if(!d->swap_since)d->swap_since=us;
        else if(us-d->swap_since>=CW_DUO_SWAP_US) {
            d->total=cup;cup=1-d->total;d->swap_since=0;d->settling=false;d->level_valid=false;
            int32_t t=g_total;g_total=g_cup;g_cup=t;
        }
    } else d->swap_since=0;
    d->water_cg=g_total;d->out_cg=g_cup;
    if(scale==d->total)settle(d,g_total,us);
    if(g_cup>=d->out_high+CW_DUO_RISE_CG) {d->out_high=g_cup;d->out_rise_us=us;}
}
// Out flow over the last 2 s, with each scale's last value held between notifications.
static void out_flow(cw_duo_t *d,int64_t now) {
    const cw_duo_ring_t *r=&d->gain[1-d->total];
    if(!r->count)return;
    int32_t old=value_at(r,now-CW_DUO_FLOW_US);
    int64_t rate=(int64_t)(d->out_cg-old)*1000000/CW_DUO_FLOW_US;
    d->out_cgps=rate>0?(int32_t)rate:0;
    if(d->out_cgps>d->peak_cgps)d->peak_cgps=d->out_cgps;
    // The drain began at the first sample 0.3 g above the start of the window.
    if(!d->open_valid&&d->out_cgps>=CW_DUO_OPEN_CGPS)
        for(unsigned k=r->count;k-->0;) {
            const cw_duo_sample_t *s=at(r,k);
            if(s->us>now-CW_DUO_FLOW_US&&s->cg>=old+30) {d->open_valid=true;d->open_us=s->us;break;}
        }
}
void cw_duo_probe(cw_duo_t *d,int64_t us,bool valid,float celsius) {
    if(d->phase!=CW_DUO_ARMED)return;
    for(unsigned i=0;i<CW_DUO_SCALES;i++)cw_pour_probe(&d->pour[i],us,valid,celsius);
}
cw_duo_start_t cw_duo_check(cw_duo_t *d,int64_t now) {
    if(d->phase!=CW_DUO_ARMED)return CW_DUO_WAIT;
    for(unsigned i=0;i<CW_DUO_SCALES;i++) {
        int32_t zero;int64_t onset;
        cw_pour_result_t r=cw_pour_check(&d->pour[i],now,&zero,&onset);
        if(r==CW_POUR_NO_ZERO)return CW_DUO_NO_ZERO;
        if(r!=CW_POUR_START)continue;
        // The other scale's zero is its own plateau before the same trigger.
        unsigned o=1-i;int32_t other;
        if(!cw_pour_zero(&d->pour[o],d->pour[i].trigger_us,&other,NULL))return CW_DUO_NO_ZERO;
        d->zero[i]=zero;d->zero[o]=other;d->total=i;d->onset_us=onset;d->phase=CW_DUO_BREWING;
        d->swap_since=0;d->out_high=0;d->out_rise_us=onset;d->out_cgps=d->peak_cgps=0;d->open_valid=false;
        d->level_valid=false;d->settling=false;d->out_frozen=false;d->resume_since=0;
        for(unsigned k=0;k<CW_DUO_SCALES;k++) {memset(&d->gain[k],0,sizeof d->gain[k]);push(&d->gain[k],now,d->raw[k]-d->zero[k]);}
        d->water_cg=d->raw[i]-zero;d->out_cg=d->raw[o]-other;
        return CW_DUO_START;
    }
    return CW_DUO_WAIT;
}
cw_duo_event_t cw_duo_step(cw_duo_t *d,int64_t now) {
    unsigned cup=1-d->total;
    if(d->phase==CW_DUO_BREWING) {
        out_flow(d,now);
        // A settled water level 30 g below the previous one: the dripper was lifted.
        if(settled(d)) {
            int32_t v=(d->settle_min+d->settle_max)/2;
            if(d->level_valid&&v<=d->level_cg-CW_DUO_LIFT_CG) {
                d->phase=CW_DUO_ENDED;d->end_us=d->level_us;d->end_water=d->level_cg;d->end_out=d->out_cg;d->out_frozen=false;d->resume_since=0;
                return CW_DUO_LIFTED;
            }
            if(!d->level_valid||v>d->level_cg-CW_DUO_LIFT_CG) {d->level_cg=v;d->level_us=d->settle_last;d->level_valid=true;}
        }
        int32_t water_spread,out_spread;
        if(now-d->onset_us>=CW_DUO_QUIET_US&&range(&d->gain[d->total],now,CW_DUO_QUIET_US,&water_spread)&&range(&d->gain[cup],now,CW_DUO_QUIET_US,&out_spread)&&
           water_spread<CW_DUO_POUR_QUIET_CG&&out_spread<CW_DUO_DRIP_QUIET_CG&&d->out_cg>=CW_DUO_MIN_OUT_CG&&2*d->out_cg>=d->water_cg) {
            d->phase=CW_DUO_ENDED;d->end_us=d->out_rise_us>d->onset_us?d->out_rise_us:d->onset_us;
            d->end_water=d->water_cg;d->end_out=d->out_cg;d->out_frozen=false;d->resume_since=0;d->out_cgps=0;
            return CW_DUO_DRAINED;
        }
        return CW_DUO_NONE;
    }
    if(d->phase!=CW_DUO_ENDED)return CW_DUO_NONE;
    // Late drips raise the beverage; lifting the server (sampling, serving) freezes it.
    if(!d->out_frozen) {
        if(d->out_cg>d->end_out)d->end_out=d->out_cg;
        else if(d->out_cg<=d->end_out-CW_DUO_FREEZE_CG)d->out_frozen=true;
    }
    if(d->water_cg>=d->end_water+CW_DUO_RESUME_CG) {
        if(!d->resume_since)d->resume_since=now;
        else if(now-d->resume_since>=CW_DUO_RESUME_US) {
            d->phase=CW_DUO_BREWING;d->resume_since=0;d->settling=false;d->level_valid=false;d->out_frozen=false;
            return CW_DUO_RESUMED;
        }
    } else d->resume_since=0;
    return CW_DUO_NONE;
}
