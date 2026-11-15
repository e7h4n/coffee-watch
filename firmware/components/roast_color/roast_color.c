// SPDX-License-Identifier: MIT
#include "roast_color.h"
#include <math.h>
#include <string.h>
#define INTERSECTION 117.f
#define DEVIATION 0.165f
float rc_index(float raw) {float x=raw/1000.f;return x-(INTERSECTION-x)*DEVIATION;}
float rc_raw_for_index(float index) {return (index+INTERSECTION*DEVIATION)/(1+DEVIATION)*1000.f;}

void rc_window_reset(rc_window_t *w) {memset(w,0,sizeof *w);}
static void window_restart(rc_window_t *w,uint32_t raw,int64_t us) {
    w->active=true;w->min=w->max=raw;w->sum=raw;w->n=1;w->first=w->last=us;w->progress=0;
}
bool rc_window_add(rc_window_t *w,uint32_t raw,int64_t us,float swing,int64_t span_us,float *mean) {
    if(!w->active||us-w->last>RC_GAP_US||us<w->last) {window_restart(w,raw,us);return false;}
    uint32_t lo=raw<w->min?raw:w->min,hi=raw>w->max?raw:w->max;
    double average=(double)(w->sum+raw)/(w->n+1);
    if(hi-lo>swing*average) {window_restart(w,raw,us);return false;}
    w->min=lo;w->max=hi;w->sum+=raw;w->n++;w->last=us;
    int64_t elapsed=us-w->first;
    w->progress=elapsed>=span_us?1.f:(float)elapsed/(float)span_us;
    if(elapsed<span_us)return false;
    if(mean)*mean=(float)((double)w->sum/w->n);
    return true;
}

void rc_meter_init(rc_meter_t *m,int64_t start_us) {memset(m,0,sizeof *m);m->ready_us=start_us+RC_WARMUP_US;}
void rc_meter_clear(rc_meter_t *m) {
    m->count=0;m->mean=m->sd=0;m->retest=false;
    // A sample already on the glass is measured again from a fresh window.
    m->captured=false;rc_window_reset(&m->window);
}
static void summarize(rc_meter_t *m) {
    double sum=0,squares=0;
    for(unsigned i=0;i<m->count;i++)sum+=m->readings[i];
    m->mean=(float)(sum/m->count);
    for(unsigned i=0;i<m->count;i++) {double d=m->readings[i]-m->mean;squares+=d*d;}
    m->sd=m->count>1?(float)sqrt(squares/(m->count-1)):0;
    m->retest=m->sd>RC_RETEST_SD;
}
unsigned rc_meter_sample(rc_meter_t *m,uint32_t raw,int64_t us) {
    unsigned events=0;
    m->raw=raw;m->raw_us=us;m->have_raw=true;
    bool loaded=m->loaded?raw>=RC_UNLOAD_RAW:raw>=RC_LOAD_RAW;
    if(loaded!=m->loaded) {
        m->loaded=loaded;rc_window_reset(&m->window);
        if(loaded) {
            events|=RC_EV_LOAD;
            // Placing the housing after a finished group starts the next one.
            if(m->count>=RC_GROUP) {m->count=0;m->mean=m->sd=0;m->retest=false;}
        } else {events|=RC_EV_UNLOAD;m->captured=false;}
    }
    m->saturated=loaded&&raw>=RC_SATURATED_RAW;
    if(!loaded||m->captured)return events;
    if(m->saturated||us<m->ready_us) {rc_window_reset(&m->window);return events;}
    float mean;
    if(rc_window_add(&m->window,raw,us,RC_SWING,RC_WINDOW_US,&mean)) {
        m->captured=true;m->readings[m->count++]=rc_index(mean);summarize(m);events|=RC_EV_READING;
        if(m->count>=RC_GROUP)events|=RC_EV_RESULT;
    }
    return events;
}
rc_phase_t rc_meter_phase(const rc_meter_t *m,int64_t now_us) {
    if(now_us<m->ready_us)return RC_WARMUP;
    if(m->loaded&&m->saturated)return RC_SATURATED;
    if(m->loaded&&!m->captured)return RC_MEASURING;
    if(m->count>=RC_GROUP)return RC_RESULT;
    if(m->loaded)return RC_READING;
    return RC_PLACE;
}

static bool begin_step(rc_cal_t *c,rc_step_t step,uint8_t pa,uint8_t *set_pa) {
    c->phase=RC_CAL_STEP;c->step=step;c->pa=pa;c->step_started=false;c->sum=0;c->n=0;
    *set_pa=pa;return true;
}
static bool fail(rc_cal_t *c,rc_cal_error_t error,uint8_t *set_pa) {
    c->phase=RC_CAL_FAILED;c->error=error;c->pa=c->original;*set_pa=c->original;return true;
}
void rc_cal_start(rc_cal_t *c,uint8_t current_pa) {
    memset(c,0,sizeof *c);c->phase=RC_CAL_PLACE;c->original=c->pa=current_pa?current_pa:RC_DEFAULT_PA;
}
float rc_cal_progress(const rc_cal_t *c) {
    if(c->phase==RC_CAL_DONE)return 1;
    if(c->phase!=RC_CAL_STEP)return 0;
    float p=(float)c->steps/RC_CAL_STEPS;return p<1?p:1;
}
bool rc_cal_sample(rc_cal_t *c,uint32_t raw,uint8_t sample_pa,int64_t us,uint8_t *set_pa) {
    const float target=rc_raw_for_index(RC_SODA_INDEX);
    switch(c->phase) {
    case RC_CAL_PLACE:
        if(sample_pa==c->pa&&raw>=RC_LOAD_RAW) {c->phase=RC_CAL_SETTLE;rc_window_reset(&c->window);rc_window_add(&c->window,raw,us,RC_SWING,RC_WINDOW_US,NULL);}
        return false;
    case RC_CAL_SETTLE:
        if(sample_pa!=c->pa)return false;
        if(raw<RC_UNLOAD_RAW) {c->phase=RC_CAL_PLACE;return false;}
        // A saturated reading is flat, so it settles too; it only means "bright enough".
        if(!rc_window_add(&c->window,raw,us,RC_SWING,RC_WINDOW_US,NULL))return false;
        return begin_step(c,RC_STEP_TOP,255,set_pa);
    case RC_CAL_STEP:break;
    default:return false;
    }
    if(sample_pa!=c->pa)return false;
    if(!c->step_started) {c->step_started=true;c->step_us=us;return false;}
    if(us-c->step_us<RC_CAL_SKIP_US)return false;
    if(!c->n) {c->min=c->max=raw;}
    if(raw<c->min)c->min=raw;
    if(raw>c->max)c->max=raw;
    c->sum+=raw;c->n++;
    if(us-c->step_us<RC_CAL_SKIP_US+RC_CAL_AVERAGE_US)return false;
    uint32_t mean=(uint32_t)((c->sum+c->n/2)/c->n);
    if(c->max-c->min>RC_CAL_SWING*mean&&c->max<RC_SATURATED_RAW)return fail(c,RC_CAL_MOVED,set_pa);
    uint32_t first=c->measured[c->pa];
    if(c->step!=RC_STEP_VERIFY)c->measured[c->pa]=mean?mean:1;
    c->steps++;
    switch(c->step) {
    case RC_STEP_TOP:
        if(mean<target)return fail(c,RC_CAL_TOO_DARK,set_pa);
        c->lo=1;c->hi=255;
        break;
    case RC_STEP_BISECT:
        if(mean>=target)c->hi=c->pa;else c->lo=c->pa+1;
        break;
    case RC_STEP_BELOW:break;
    case RC_STEP_VERIFY:
        c->index=rc_index((float)mean);
        if(fabsf((float)mean-(float)first)>RC_CAL_SWING*first)return fail(c,RC_CAL_MOVED,set_pa);
        c->phase=RC_CAL_DONE;return false;
    }
    if(c->lo<c->hi)return begin_step(c,RC_STEP_BISECT,(uint8_t)((c->lo+c->hi)/2),set_pa);
    // lo is the smallest amplitude reading at least the target; its neighbour reads less.
    uint8_t b=c->lo;
    if(b>1&&!c->measured[b-1])return begin_step(c,RC_STEP_BELOW,(uint8_t)(b-1),set_pa);
    c->chosen=b;
    if(b>1&&target-c->measured[b-1]<c->measured[b]-target)c->chosen=(uint8_t)(b-1);
    return begin_step(c,RC_STEP_VERIFY,c->chosen,set_pa);
}
