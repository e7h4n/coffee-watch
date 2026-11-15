// SPDX-License-Identifier: MIT
#include "roast_color.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define STEP_US 80000LL
static int scenarios;
static bool near(float a,float b,float tolerance) {return fabsf(a-b)<=tolerance;}
// Feeds a constant raw value for duration_us at the sensor's 12.5 Hz output rate.
static unsigned feed(rc_meter_t *m,uint32_t raw,int64_t *us,int64_t duration_us) {
    unsigned events=0;
    for(int64_t end=*us+duration_us;*us<end;*us+=STEP_US)events|=rc_meter_sample(m,raw,*us);
    return events;
}
static void formula(void) {
    assert(near(rc_index(117000),117,1e-4f));
    // The Roast Meter integer form, round(x - (117 - x) * 0.165).
    assert(lroundf(rc_index(80000))==lroundf(80-(117-80)*0.165f));
    assert(near(rc_raw_for_index(230),213996,1));
    assert(near(rc_index(rc_raw_for_index(78.4f)),78.4f,1e-3f));
    scenarios++;
}
static void warmup_and_lift_between_readings(void) {
    rc_meter_t m;int64_t us=0;rc_meter_init(&m,0);
    assert(rc_meter_phase(&m,us)==RC_WARMUP);
    unsigned e=feed(&m,90000,&us,RC_WARMUP_US);
    assert(e==RC_EV_LOAD&&m.count==0&&rc_meter_phase(&m,us-1)==RC_WARMUP);
    assert(rc_meter_phase(&m,us)==RC_MEASURING);
    e=feed(&m,90000,&us,RC_WINDOW_US-STEP_US);assert(!(e&RC_EV_READING));
    e=feed(&m,90000,&us,STEP_US+1);assert(e==RC_EV_READING&&m.count==1&&near(m.readings[0],rc_index(90000),1e-3f));
    assert(rc_meter_phase(&m,us)==RC_READING);
    // Staying on the sample never adds a reading: the housing must be lifted.
    e=feed(&m,90000,&us,10*RC_WINDOW_US);assert(!e&&m.count==1);
    e=feed(&m,5000,&us,STEP_US);assert(e==RC_EV_UNLOAD&&rc_meter_phase(&m,us)==RC_PLACE);
    e=feed(&m,90500,&us,RC_WINDOW_US+STEP_US);assert((e&RC_EV_LOAD)&&(e&RC_EV_READING)&&m.count==2);
    scenarios++;
}
static void window_restarts(void) {
    rc_meter_t m;int64_t us=RC_WARMUP_US;rc_meter_init(&m,0);
    feed(&m,90000,&us,RC_WINDOW_US-4*STEP_US);
    // A 0.5 % jump (pressing the housing) restarts the 2 s window.
    assert(!(feed(&m,90450,&us,STEP_US)&RC_EV_READING));
    assert(m.window.n==1);
    assert(!(feed(&m,90450,&us,RC_WINDOW_US-STEP_US)&RC_EV_READING));
    assert(feed(&m,90450,&us,2*STEP_US)&RC_EV_READING);
    // A missing-data gap restarts it as well.
    rc_meter_init(&m,0);us=RC_WARMUP_US;
    feed(&m,90000,&us,RC_WINDOW_US-STEP_US);us+=RC_GAP_US+STEP_US;
    assert(!(feed(&m,90000,&us,STEP_US)&RC_EV_READING)&&m.window.n==1);
    // Spread within 0.3 % still settles.
    rc_meter_init(&m,0);us=RC_WARMUP_US;unsigned e=0;
    for(int i=0;i<40;i++) {e|=rc_meter_sample(&m,i%2?90000:90200,us);us+=STEP_US;}
    assert(e&RC_EV_READING);
    scenarios++;
}
static void group_result_and_retest(void) {
    rc_meter_t m;int64_t us=RC_WARMUP_US;rc_meter_init(&m,0);
    const uint32_t close[3]={80000,80500,79500};
    unsigned e=0;
    for(int i=0;i<3;i++) {e=feed(&m,close[i],&us,RC_WINDOW_US+2*STEP_US);feed(&m,1000,&us,STEP_US);}
    assert(m.count==3&&!m.retest&&near(m.mean,rc_index(80000),0.01f));
    assert(near(m.sd,0.5825f,0.001f)&&rc_meter_phase(&m,us)==RC_RESULT);
    // The result stays until the next placement, which starts a new group.
    e=feed(&m,70000,&us,STEP_US);assert(e==RC_EV_LOAD&&m.count==0&&rc_meter_phase(&m,us)==RC_MEASURING);
    const uint32_t spread[3]={70000,72000,74000};
    us+=STEP_US;feed(&m,1000,&us,STEP_US);e=0;
    for(int i=0;i<3;i++) {e|=feed(&m,spread[i],&us,RC_WINDOW_US+2*STEP_US);feed(&m,1000,&us,STEP_US);}
    assert((e&RC_EV_RESULT)&&m.count==3&&m.retest&&m.sd>RC_RETEST_SD);
    // Clearing keeps a sample on the glass measurable from a fresh window.
    feed(&m,75000,&us,RC_WINDOW_US+2*STEP_US);assert(m.count==1);
    rc_meter_clear(&m);assert(m.count==0&&rc_meter_phase(&m,us)==RC_MEASURING);
    assert(feed(&m,75000,&us,RC_WINDOW_US+2*STEP_US)&RC_EV_READING);
    scenarios++;
}
static void hysteresis_and_saturation(void) {
    rc_meter_t m;int64_t us=RC_WARMUP_US;rc_meter_init(&m,0);
    assert(!feed(&m,29000,&us,STEP_US)&&!m.loaded);
    assert(feed(&m,30000,&us,STEP_US)==RC_EV_LOAD);
    assert(!feed(&m,29000,&us,STEP_US)&&m.loaded);
    assert(feed(&m,27999,&us,STEP_US)==RC_EV_UNLOAD);
    feed(&m,RC_RAW_MAX,&us,3*RC_WINDOW_US);
    assert(m.count==0&&rc_meter_phase(&m,us)==RC_SATURATED);
    scenarios++;
}
// Simulated glass + baking soda: raw is proportional to the IR amplitude.
typedef struct {double per_pa;uint8_t pa;int64_t us;} sensor_t;
static uint32_t reading(const sensor_t *s) {double r=s->per_pa*s->pa;return r>RC_RAW_MAX?RC_RAW_MAX:(uint32_t)r;}
static unsigned run(rc_cal_t *c,sensor_t *s,int limit,uint32_t (*value)(const sensor_t*,int)) {
    unsigned changes=0;
    for(int i=0;i<limit&&(c->phase!=RC_CAL_DONE&&c->phase!=RC_CAL_FAILED);i++) {
        uint8_t pa=0;
        if(rc_cal_sample(c,value?value(s,i):reading(s),s->pa,s->us,&pa)) {s->pa=pa;changes++;}
        s->us+=STEP_US;
    }
    return changes;
}
static void calibration(void) {
    rc_cal_t c;sensor_t s={213996/150.3,135,0};
    rc_cal_start(&c,135);assert(c.phase==RC_CAL_PLACE&&rc_cal_progress(&c)==0);
    unsigned changes=run(&c,&s,2000,NULL);
    // 150 reads 229.5 and 151 reads 231.2: the nearer one is chosen and verified.
    assert(c.phase==RC_CAL_DONE&&c.chosen==150&&s.pa==150&&near(c.index,229.5f,0.1f));
    assert(c.steps<=RC_CAL_STEPS&&changes==c.steps&&rc_cal_progress(&c)==1);
    // About a second per step, plus the 2 s placement window.
    assert(s.us<(int64_t)(RC_CAL_STEPS+3)*1100000);
    // Saturating at the default amplitude is still bright enough to calibrate.
    sensor_t bright={213996/40.6,135,0};rc_cal_start(&c,135);run(&c,&bright,2000,NULL);
    assert(c.phase==RC_CAL_DONE&&c.chosen==41&&bright.pa==41);
    // At a low amplitude one step is several index points; the nearest still passes.
    sensor_t coarse={213996/20.45,135,0};rc_cal_start(&c,135);run(&c,&coarse,2000,NULL);
    assert(c.phase==RC_CAL_DONE&&(c.chosen==20||c.chosen==21)&&fabsf(c.index-RC_SODA_INDEX)>3);
    scenarios++;
}
static uint32_t lifted_after_two_steps(const sensor_t *s,int i) {(void)i;return s->us>5500000?3000:reading(s);}
static uint32_t jitter(const sensor_t *s,int i) {uint32_t r=reading(s);return i%2?r:r*1.03;}
// The final repeat reads 5 % lower than the first measurement at that amplitude.
static const rc_cal_t *watched;
static uint32_t slid_before_verify(const sensor_t *s,int i) {(void)i;return watched->step==RC_STEP_VERIFY?reading(s)*0.95:reading(s);}
static void calibration_failures(void) {
    rc_cal_t c;
    // Coffee instead of baking soda cannot reach 230 even at 255.
    sensor_t dark={500,135,0};rc_cal_start(&c,135);run(&c,&dark,2000,NULL);
    assert(c.phase==RC_CAL_FAILED&&c.error==RC_CAL_TOO_DARK&&dark.pa==135);
    sensor_t s={213996/150.3,120,0};rc_cal_start(&c,120);run(&c,&s,2000,lifted_after_two_steps);
    assert(c.phase==RC_CAL_FAILED&&c.error==RC_CAL_MOVED&&s.pa==120);
    sensor_t slid={213996/150.3,135,0};rc_cal_start(&c,135);watched=&c;run(&c,&slid,2000,slid_before_verify);
    assert(c.phase==RC_CAL_FAILED&&c.error==RC_CAL_MOVED&&c.step==RC_STEP_VERIFY&&slid.pa==135);
    sensor_t shaky={213996/150.3,135,0};rc_cal_start(&c,135);
    // A placement that never settles does not start the steps.
    run(&c,&shaky,200,jitter);assert(c.phase==RC_CAL_SETTLE&&shaky.pa==135);
    // Samples still carrying the old amplitude are ignored.
    rc_cal_start(&c,135);uint8_t pa=0;
    for(int i=0;i<40;i++)assert(!rc_cal_sample(&c,200000,99,i*STEP_US,&pa));
    assert(c.phase==RC_CAL_PLACE);
    scenarios++;
}
int main(void) {
    formula();warmup_and_lift_between_readings();window_restarts();group_result_and_retest();
    hysteresis_and_saturation();calibration();calibration_failures();
    printf("%d roast color scenarios passed\n",scenarios);
    return 0;
}
