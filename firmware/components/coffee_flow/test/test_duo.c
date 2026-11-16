// SPDX-License-Identifier: MIT
#include "duo.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// A stacked stand at 10 Hz per scale: the total scale carries everything, the cup
// scale only the server. Water poured stays on the total scale; the beverage moves
// from the dripper to the server, so it shows on both.
typedef struct {
    cw_duo_t d;unsigned total,cup;double base_total,base_cup;
    double water,out;double bump_total,bump_cup;double temp;bool cup_on_change;int32_t last_cup;
    int64_t us;cw_duo_event_t last;int starts;cw_duo_start_t start;
} rig_t;
static void rig_init(rig_t *r,unsigned total) {
    memset(r,0,sizeof *r);cw_duo_reset(&r->d);cw_duo_rearm(&r->d);r->total=total;r->cup=1-total;
    r->base_total=2500;r->base_cup=400;r->temp=22;r->last_cup=INT32_MIN;
}
static int32_t cg(double g) {return (int32_t)lround(g*10)*10;}
// One 100 ms step: total scale, probe, then cup scale 50 ms later.
static void step(rig_t *r) {
    r->us+=100000;
    int32_t t=cg(r->base_total+r->bump_total+r->water),c=cg(r->base_cup+r->bump_cup+r->out);
    cw_duo_weight(&r->d,r->total,r->us,t);
    cw_duo_probe(&r->d,r->us+1,r->temp>25,(float)r->temp);
    if(r->d.phase==CW_DUO_ARMED) {cw_duo_start_t s=cw_duo_check(&r->d,r->us+1);if(s!=CW_DUO_WAIT)r->start=s;if(s==CW_DUO_START)r->starts++;}
    else {cw_duo_event_t e=cw_duo_step(&r->d,r->us+1);if(e)r->last=e;}
    if(!r->cup_on_change||c!=r->last_cup) {
        r->last_cup=c;cw_duo_weight(&r->d,r->cup,r->us+50000,c);
        if(r->d.phase==CW_DUO_ARMED) {cw_duo_start_t s=cw_duo_check(&r->d,r->us+50001);if(s!=CW_DUO_WAIT)r->start=s;if(s==CW_DUO_START)r->starts++;}
        else {cw_duo_event_t e=cw_duo_step(&r->d,r->us+50001);if(e)r->last=e;}
    }
}
static double sec(const rig_t *r) {return r->us/1e6;}
static int scenarios;
static void settle_ready(rig_t *r) {for(int i=0;i<100;i++)step(r);assert(r->d.phase==CW_DUO_ARMED);}
// Switch: two pours to 230 g, steep with the valve closed, open at 100 s, drain to 196 g.
static void switch_brew(unsigned total,bool cup_on_change) {
    rig_t r;rig_init(&r,total);r.cup_on_change=cup_on_change;settle_ready(&r);
    int64_t t0=r.us;
    for(;;) {
        double t=(r.us-t0)/1e6;
        r.water=t<18?120*t/18:t<23?120:t<35?120+110*(t-23)/12:230;
        r.temp=t<1.5?22:91-(t-1.5)*0.06;
        r.out=t<100?0:t<190?196*(1-exp(-(t-100)/20)):196*(1-exp(-90.0/20));
        step(&r);
        if(r.last==CW_DUO_DRAINED||t>260)break;
        if(t>3&&!(r.starts==1&&r.d.total==total)) {fprintf(stderr,"t=%.1f starts=%d total=%u want=%u start=%d phase=%d\n",t,r.starts,r.d.total,total,r.start,r.d.phase);assert(0);}
        if(t>40&&t<100) {assert(r.d.phase==CW_DUO_BREWING&&r.d.water_cg==23000&&r.d.out_cg==0);}
    }
    assert(r.last==CW_DUO_DRAINED&&r.d.phase==CW_DUO_ENDED);
    double end=(r.d.end_us-t0)/1e6,stop=(r.us-t0)/1e6;
    assert(fabs((r.d.onset_us-t0)/1e6)<0.3);
    assert(r.d.end_water==23000&&abs(r.d.end_out-cg(196*(1-exp(-90.0/20))))<=10);
    // The end is the last 0.1 g rise; the 5 s quiet window confirms it a few seconds later.
    assert(end>185&&end<=190.2&&stop>end&&stop<end+5.6);
    assert(r.d.open_valid&&fabs((r.d.open_us-t0)/1e6-100)<0.5);
    assert(r.d.peak_cgps>800&&r.d.peak_cgps<1000);
    (void)sec;scenarios++;
}
// V60 pulses: bloom 40 g, two more pours to 250 g; the dripper drains through a bed that
// keeps 30 g. Drips never pause long enough between pours to look drained.
static void v60_brew(void) {
    rig_t r;rig_init(&r,0);settle_ready(&r);int64_t t0=r.us;double in=0,liquid=0;
    for(;;) {
        double t=(r.us-t0)/1e6,pour=0;
        if(t<8)pour=5;else if(t>=45&&t<57)pour=110.0/12;else if(t>=80&&t<91)pour=100.0/11;
        in+=pour*0.1;liquid+=pour*0.1;double drip=liquid>30?0.06*(liquid-30):0;liquid-=drip*0.1;
        r.water=in;r.out+=drip*0.1;r.temp=t<1?22:90;
        step(&r);
        if(r.last==CW_DUO_DRAINED||t>400)break;
    }
    double end=(r.d.end_us-t0)/1e6;
    assert(r.last==CW_DUO_DRAINED&&r.d.end_water==25000&&end>150);
    assert(r.d.end_out>=20000&&r.d.end_out<=22000);
    scenarios++;
}
// A bump at the trigger makes the cup scale rise first; the larger gain corrects it.
static void swap_roles(void) {
    rig_t r;rig_init(&r,1);settle_ready(&r);int64_t t0=r.us;
    for(int i=0;i<4;i++) {r.bump_cup=1.0;r.temp=60;step(&r);} // cup bumped as the probe warms
    assert(r.starts==1&&r.d.total==0);
    for(;;) {double t=(r.us-t0)/1e6;r.bump_cup=1.0;r.water=(t-0.4)*8;step(&r);if(t>4)break;}
    assert(r.d.total==1&&abs(r.d.water_cg-cg(r.water))<=20&&abs(r.d.out_cg-100)<=10);
    scenarios++;
}
// Lifting the dripper ends the brew at the level before the lift.
static void lift(void) {
    rig_t r;rig_init(&r,1);settle_ready(&r);int64_t t0=r.us;
    for(;;) {
        double t=(r.us-t0)/1e6;
        r.water=t<20?10*t:200;r.out=t<10?0:t<60?(t-10)*2:100;r.temp=t<1?22:90;
        if(t>=60)r.water=200-450; // dripper, grounds and retained water leave the stand
        step(&r);if(r.last||t>80)break;
    }
    assert(r.last==CW_DUO_LIFTED&&abs(r.d.end_water-20000)<=20&&r.d.end_out==10000);
    scenarios++;
}
// After the end: more water resumes, late drips count, lifting the server freezes it.
static void after_end(void) {
    rig_t r;rig_init(&r,1);settle_ready(&r);int64_t t0=r.us;
    for(;;) {double t=(r.us-t0)/1e6;r.water=t<10?20*t:200;r.out=t<15?0:t<60?(t-15)*4:180;r.temp=t<1?22:90;step(&r);if(r.last==CW_DUO_DRAINED||t>120)break;}
    assert(r.last==CW_DUO_DRAINED&&r.d.end_out==18000);
    r.out=180.4;for(int i=0;i<10;i++)step(&r);assert(r.d.end_out==18040&&r.d.phase==CW_DUO_ENDED); // a late drip
    r.water=210;r.last=CW_DUO_NONE;for(int i=0;i<10;i++)step(&r);assert(r.last==CW_DUO_RESUMED&&r.d.phase==CW_DUO_BREWING);
    for(int i=0;i<300&&r.last!=CW_DUO_DRAINED;i++) {r.out=r.out<188?r.out+0.4:188;step(&r);}
    assert(r.last==CW_DUO_DRAINED&&r.d.end_water==21000&&r.d.end_out==18800);
    r.bump_cup=-600;r.bump_total=-600;for(int i=0;i<10;i++)step(&r); // server lifted to serve
    r.bump_cup=-20;r.bump_total=-20;for(int i=0;i<10;i++)step(&r);   // back, 20 g poured off
    assert(r.d.out_frozen&&r.d.end_out==18800&&r.d.phase==CW_DUO_ENDED);
    (void)t0;scenarios++;
}
// The cup scale moving for the whole 5 s before the trigger leaves it without a zero.
static void no_zero(void) {
    rig_t r;rig_init(&r,1);settle_ready(&r);
    for(int i=0;i<120;i++) {r.bump_cup=(i%2)?1.5:-1.5;r.temp=i<60?22:90;r.water=i<60?0:(i-60)*1.0;step(&r);if(r.start)break;}
    assert(r.start==CW_DUO_NO_ZERO&&r.d.phase==CW_DUO_ARMED);
    scenarios++;
}
// A warm hand on the probe without water never starts.
static void no_water(void) {
    rig_t r;rig_init(&r,0);settle_ready(&r);
    for(int i=0;i<50;i++) {r.temp=40;step(&r);}
    assert(r.starts==0&&r.d.phase==CW_DUO_ARMED);
    scenarios++;
}
int main(void) {
    switch_brew(1,false);switch_brew(0,false);switch_brew(1,true);v60_brew();swap_roles();lift();after_end();no_zero();no_water();
    printf("%d duo scenarios passed\n",scenarios);
    return 0;
}
