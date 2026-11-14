// SPDX-License-Identifier: MIT
#include "pour.h"
#include <math.h>
#include <string.h>
void cw_pour_reset(cw_pour_t *p) {memset(p,0,sizeof *p);}
void cw_pour_rearm(cw_pour_t *p) {p->temp_count=p->temp_next=0;p->triggered=false;p->trigger_us=0;}
// k = 0 is the newest entry.
static const cw_pour_weight_t *weight_at(const cw_pour_t *p,unsigned k)
{return &p->weights[(p->weight_next+CW_POUR_WEIGHTS-1-k)%CW_POUR_WEIGHTS];}
static const cw_pour_temp_t *temp_at(const cw_pour_t *p,unsigned k)
{return &p->temps[(p->temp_next+CW_POUR_TEMPS-1-k)%CW_POUR_TEMPS];}
void cw_pour_weight(cw_pour_t *p,int64_t us,int32_t cg) {
    if(p->weight_count&&us<=weight_at(p,0)->us)return;
    p->weights[p->weight_next]=(cw_pour_weight_t){.us=us,.cg=cg};
    p->weight_next=(p->weight_next+1)%CW_POUR_WEIGHTS;
    if(p->weight_count<CW_POUR_WEIGHTS)p->weight_count++;
}
static int32_t median(int32_t *v,unsigned n) {
    for(unsigned i=1;i<n;i++) {int32_t x=v[i];unsigned j=i;while(j&&v[j-1]>x) {v[j]=v[j-1];j--;}v[j]=x;}
    return n%2?v[n/2]:(int32_t)(((int64_t)v[n/2-1]+v[n/2])/2);
}
bool cw_pour_probe(cw_pour_t *p,int64_t us,bool valid,float celsius) {
    if(p->temp_count&&us<=temp_at(p,0)->us)return false;
    valid=valid&&isfinite(celsius)&&celsius>-50&&celsius<150;
    int16_t cc=valid?(int16_t)lroundf(celsius*100):0;
    int32_t values[CW_POUR_TEMPS];unsigned seen=0,count=0;
    for(unsigned k=0;k<p->temp_count;k++) {
        const cw_pour_temp_t *t=temp_at(p,k);
        if(t->us<us-CW_POUR_BASELINE_US)break;
        if(t->us>us-CW_POUR_BASELINE_SKIP_US)continue;
        seen++;if(t->valid)values[count++]=t->cc;
    }
    p->temps[p->temp_next]=(cw_pour_temp_t){.us=us,.cc=cc,.valid=valid};
    p->temp_next=(p->temp_next+1)%CW_POUR_TEMPS;
    if(p->temp_count<CW_POUR_TEMPS)p->temp_count++;
    // A cold probe reads as missing below the ADC floor; a mostly-missing window
    // therefore means "below ~25 C", while too few samples mean no baseline yet.
    int32_t baseline;
    if(count>=5)baseline=median(values,count);
    else if(seen>=10)baseline=CW_POUR_FLOOR_CC;
    else return false;
    if(!valid||p->triggered||cc-baseline<CW_POUR_RISE_CC)return false;
    p->triggered=true;p->trigger_us=us;return true;
}
bool cw_pour_zero(const cw_pour_t *p,int64_t end,int32_t *zero_cg,int64_t *plateau_end_us) {
    unsigned first=0;
    while(first<p->weight_count&&weight_at(p,first)->us>end)first++;
    for(unsigned j=first;j<p->weight_count;j++) {
        const cw_pour_weight_t *last=weight_at(p,j);
        if(last->us<end-CW_POUR_LOOKBACK_US)break;
        int32_t lo=last->cg,hi=last->cg;unsigned i=j;
        while(i+1<p->weight_count) {
            const cw_pour_weight_t *older=weight_at(p,i+1),*newer=weight_at(p,i);
            if(older->us<end-CW_POUR_LOOKBACK_US||newer->us-older->us>CW_POUR_GAP_US)break;
            int32_t l=older->cg<lo?older->cg:lo,h=older->cg>hi?older->cg:hi;
            if(h-l>CW_POUR_SPREAD_CG)break;
            lo=l;hi=h;i++;
        }
        if(last->us-weight_at(p,i)->us<CW_POUR_PLATEAU_US)continue;
        int32_t values[CW_POUR_WEIGHTS];unsigned n=0;
        for(unsigned k=j;k<=i;k++)values[n++]=weight_at(p,k)->cg;
        if(zero_cg)*zero_cg=median(values,n);
        if(plateau_end_us)*plateau_end_us=last->us;
        return true;
    }
    return false;
}
cw_pour_result_t cw_pour_check(cw_pour_t *p,int64_t now,int32_t *zero_cg,int64_t *onset_us) {
    if(!p->triggered||!p->weight_count)return CW_POUR_WAIT;
    bool expired=now-p->trigger_us>CW_POUR_CONFIRM_US;
    int32_t zero;int64_t plateau_end;
    if(!cw_pour_zero(p,p->trigger_us,&zero,&plateau_end)) {
        if(!expired)return CW_POUR_WAIT;
        p->triggered=false;
        // Weight moved around the trigger but never settled: water without a zero.
        int32_t lo=INT32_MAX,hi=INT32_MIN;
        for(unsigned k=0;k<p->weight_count;k++) {
            const cw_pour_weight_t *w=weight_at(p,k);
            if(w->us<p->trigger_us-CW_POUR_LOOKBACK_US)break;
            if(w->cg<lo)lo=w->cg;
            if(w->cg>hi)hi=w->cg;
        }
        return hi!=INT32_MIN&&hi-lo>=CW_POUR_WATER_CG?CW_POUR_NO_ZERO:CW_POUR_WAIT;
    }
    if(weight_at(p,0)->cg<zero+CW_POUR_WATER_CG) {
        if(expired)p->triggered=false; // warm hand or steam: no water followed
        return CW_POUR_WAIT;
    }
    // Onset is the first sample after the plateau that left it upwards.
    int64_t onset=weight_at(p,0)->us;
    for(unsigned k=0;k<p->weight_count;k++) {
        const cw_pour_weight_t *w=weight_at(p,k);
        if(w->us<=plateau_end)break;
        if(w->cg>=zero+CW_POUR_ONSET_CG)onset=w->us;
    }
    p->triggered=false;
    if(zero_cg)*zero_cg=zero;
    if(onset_us)*onset_us=onset;
    return CW_POUR_START;
}
