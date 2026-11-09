// SPDX-License-Identifier: MIT
#include "brew_trace.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static double ratio_for(double r) {return 2*r/(CW_PROBE_R_PULLUP+r);}
int main(void) {
    // 100k at 25 C; B3950 gives ~9.33k at 90 C and ~6.98k at 100 C.
    assert(fabs(cw_probe_celsius(ratio_for(100000))-25)<0.01);
    assert(fabs(cw_probe_celsius(ratio_for(9330))-90)<0.1);
    double hot=cw_probe_celsius(ratio_for(6977));assert(hot>99.8&&hot<100.2);
    assert(isnan(cw_probe_celsius(1.999))&&isnan(cw_probe_celsius(0))&&isnan(cw_probe_celsius(NAN)));
    assert(isnan(cw_probe_celsius(ratio_for(2000000))));
    assert(cw_probe_cc(93.256)==9326&&cw_probe_cc(-1.005)==-100&&cw_probe_cc(NAN)==CW_TRACE_NO_PROBE);
    cw_trace_sample_t buf[4];cw_trace_t t;cw_trace_init(&t,buf,4);
    assert(!cw_trace_append(&t,0,0,0));
    cw_trace_start(&t,1000000);
    assert(cw_trace_append(&t,1000000,0,2210));
    assert(cw_trace_append(&t,1100000,CW_TRACE_NO_WEIGHT,2215));
    cw_trace_brew(&t,1250000);cw_trace_brew(&t,9000000);assert(t.brew_started&&t.brew_ms==250);
    assert(cw_trace_append(&t,1300000,1234,CW_TRACE_NO_PROBE));
    assert(cw_trace_append(&t,1400000,-56,-150));
    assert(!cw_trace_append(&t,1500000,0,0)&&t.truncated&&t.count==4);
    char csv[512];int n=cw_trace_csv(t.samples,t.count,t.brew_ms,csv,sizeof csv);assert(n>0&&(size_t)n==strlen(csv));
    assert(!strcmp(csv,"t_ms,weight_g,probe_c\n-250,0.0,22.10\n-150,,22.15\n50,12.3,\n150,-0.6,-1.50\n"));
    assert(cw_trace_csv(t.samples,t.count,t.brew_ms,csv,40)<0);
    assert((size_t)n<cw_trace_csv_bound(t.count));
    cw_trace_stop(&t);assert(!cw_trace_append(&t,1600000,0,0));
    cw_trace_start(&t,2000000);assert(t.count==0&&!t.truncated&&!t.brew_started);
    fputs(csv,stdout);puts("trace tests passed");
    return 0;
}
