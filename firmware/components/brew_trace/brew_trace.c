// SPDX-License-Identifier: MIT
#include "brew_trace.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
double cw_probe_celsius(double ratio) {
    // ratio = V(G8)/V(G9) = 2*R/(R1+R)  ->  R = R1*ratio/(2-ratio)
    if(!isfinite(ratio)||ratio<=0.002||ratio>=1.98)return NAN;
    double r=CW_PROBE_R_PULLUP*ratio/(2-ratio);
    if(r<500||r>1000000)return NAN;
    double c=1/(1/298.15+log(r/CW_PROBE_R25)/CW_PROBE_BETA)-273.15;
    return c>-20&&c<200?c:NAN;
}
int16_t cw_probe_cc(double c) {
    if(!isfinite(c)||c<=-300||c>=300)return CW_TRACE_NO_PROBE;
    return (int16_t)(c>=0?floor(c*100+.5):-floor(-c*100+.5));
}
void cw_trace_init(cw_trace_t *t,cw_trace_sample_t *storage,size_t capacity) {memset(t,0,sizeof *t);t->samples=storage;t->capacity=storage?capacity:0;}
void cw_trace_start(cw_trace_t *t,int64_t us) {t->count=0;t->active=true;t->brew_started=t->truncated=false;t->start_us=us;t->brew_ms=0;}
void cw_trace_brew(cw_trace_t *t,int64_t us) {
    if(!t->active||t->brew_started||us<t->start_us)return;
    t->brew_started=true;t->brew_ms=(uint32_t)((us-t->start_us)/1000);
}
void cw_trace_stop(cw_trace_t *t) {t->active=false;}
bool cw_trace_append(cw_trace_t *t,int64_t us,int32_t weight_cg,int16_t probe_cc) {
    if(!t->active||us<t->start_us)return false;
    if(t->count>=t->capacity) {t->truncated=true;return false;}
    t->samples[t->count++]=(cw_trace_sample_t){.t_ms=(uint32_t)((us-t->start_us)/1000),.weight_cg=weight_cg,.probe_cc=probe_cc};
    return true;
}
size_t cw_trace_csv_bound(size_t count) {return 32+count*40;}
static int32_t tenth_cg(int32_t v) {return (int32_t)((v>=0?((int64_t)v+5)/10:-((-(int64_t)v+5)/10)));}
int cw_trace_csv(const cw_trace_sample_t *s,size_t count,uint32_t brew_ms,char *out,size_t cap) {
    if(!out||(!s&&count))return -1;
    size_t used=0;int n=snprintf(out,cap,"t_ms,weight_g,probe_c\n");
    if(n<0||(size_t)n>=cap)return -1;
    used=(size_t)n;
    for(size_t i=0;i<count;i++) {
        char weight[16]="",probe[16]="";
        if(s[i].weight_cg!=CW_TRACE_NO_WEIGHT) {int32_t d=tenth_cg(s[i].weight_cg);snprintf(weight,sizeof weight,"%s%ld.%ld",d<0?"-":"",(long)(d<0?-d:d)/10,(long)(d<0?-d:d)%10);}
        if(s[i].probe_cc!=CW_TRACE_NO_PROBE) {int v=s[i].probe_cc;snprintf(probe,sizeof probe,"%s%d.%02d",v<0?"-":"",(v<0?-v:v)/100,(v<0?-v:v)%100);}
        n=snprintf(out+used,cap-used,"%lld,%s,%s\n",(long long)s[i].t_ms-(long long)brew_ms,weight,probe);
        if(n<0||(size_t)n>=cap-used)return -1;
        used+=(size_t)n;
    }
    return (int)used;
}
uint32_t cw_trace_span_ms(uint32_t elapsed_ms) {
    if(elapsed_ms<=150000)return 150000;
    if(elapsed_ms<=240000)return 240000;
    return (elapsed_ms+119999)/120000*120000;
}
uint32_t cw_trace_elapsed_ms(const cw_trace_t *t) {
    if(!t->brew_started||!t->count)return 0;
    uint32_t last=t->samples[t->count-1].t_ms;
    return last>t->brew_ms?last-t->brew_ms:0;
}
size_t cw_trace_series(const cw_trace_t *t,uint32_t span_ms,size_t columns,int16_t *probe_cc,int32_t *weight_cg) {
    for(size_t i=0;i<columns;i++) {probe_cc[i]=CW_TRACE_NO_PROBE;weight_cg[i]=CW_TRACE_NO_WEIGHT;}
    if(!t->brew_started||!columns||!span_ms)return 0;
    size_t used=0;
    for(size_t i=0;i<t->count;i++) {
        const cw_trace_sample_t *s=&t->samples[i];
        if(s->t_ms<t->brew_ms)continue;
        uint64_t column=(uint64_t)(s->t_ms-t->brew_ms)*columns/span_ms;
        if(column>=columns)break;
        probe_cc[column]=s->probe_cc;weight_cg[column]=s->weight_cg;used=(size_t)column+1;
    }
    return used;
}
