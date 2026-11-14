#include "coffee_flow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int64_t us;
static unsigned brewer=CW_V60;
static void weight(cw_flow_t *f,int32_t value,unsigned epoch) {
    bookoo_message_t m={.kind=BOOKOO_WEIGHT,.weight_cg=value};us+=100000;cw_scale_message(f,epoch,&m,us);
}
static void weights(cw_flow_t *f,int32_t value,unsigned count) {for(unsigned i=0;i<count;i++)weight(f,value,7);}
// One 100 ms step: a weight notification, then a bed-probe reading.
static void tick(cw_flow_t *f,int32_t value,float celsius) {weight(f,value,7);cw_probe_message(f,!isnan(celsius),celsius,us+1);}
static void ticks(cw_flow_t *f,int32_t value,float celsius,unsigned count) {for(unsigned i=0;i<count;i++)tick(f,value,celsius);}
static void event(cw_flow_t *f,uint8_t state) {
    bookoo_message_t m={.kind=BOOKOO_AUTO,.state=state};us+=100000;cw_scale_message(f,7,&m,us);
}
static void initialize_mode(cw_flow_t *f,uint8_t submode) {
    uint8_t p[6];assert(f->initial_step==1&&cw_initial_request(f,p)&&p[2]==15);
    cw_write_done(f,true);assert(!cw_initial_request(f,p));
    bookoo_message_t mode={.kind=BOOKOO_MODE,.mode=2,.submode=submode};
    cw_scale_message(f,7,&mode,us+1);
    if(submode==2) {
        assert(cw_initial_request(f,p)&&p[2]==24&&p[3]==1);cw_write_done(f,true);
        assert(cw_initial_request(f,p)&&p[2]==7);cw_write_done(f,true);
    }
    assert(cw_initial_request(f,p)&&p[2]==1&&p[5]==8);cw_write_done(f,true);
    assert(!f->initial_step&&!f->write_pending&&!cw_initial_request(f,p));
}
static void setup(cw_flow_t *f) {cw_init(f);cw_scale_link(f,true,7);assert(f->stage==CW_BREWER);cw_choose(f,brewer);assert(f->stage==CW_EMPTY);initialize_mode(f,2);}
// Empty server 100.0 g, stored dose 15.0 g, then the dripper and grounds: 400.0 g.
static void prepare(cw_flow_t *f) {
    setup(f);weights(f,10000,51);assert(f->stage==CW_PREP&&f->empty_cg==10000);
    bookoo_message_t m={.kind=BOOKOO_DOSE,.dose_cg=1500};cw_scale_message(f,7,&m,us+1);
    assert(f->scale.dose_valid&&f->stage==CW_READY);
    ticks(f,40000,20.f,40);
}
// There is no Start: preparation arms itself once the empty server and dose are known.
static void arm(cw_flow_t *f) {assert(f->stage==CW_READY);}
// Pour at 10 g/s; the probe meets hot water `lag` steps after the weight leaves 400.0 g.
static int64_t pour(cw_flow_t *f,unsigned lag,unsigned steps) {
    int64_t onset=0;
    for(unsigned i=1;i<=steps;i++) {
        tick(f,40000+(int32_t)i*100,i>lag?60.f:20.f);
        if(i==1)onset=us;
    }
    return onset;
}
// Pour to 700.0 g, settle, then lift the dripper: server 100.0 g + 270.0 g beverage.
static void brew(cw_flow_t *f) {
    prepare(f);arm(f);int64_t onset=pour(f,1,4);assert(f->stage==CW_BREW);
    assert(f->zero_cg==40000&&f->onset_us==onset&&f->brew_dose_cg==1500);
    for(int32_t w=40500;w<70000;w+=500)tick(f,w,90.f);
    ticks(f,70000,85.f,30);assert(f->stage==CW_BREW&&f->level_valid);
    ticks(f,37000,80.f,11);assert(f->stage==CW_BEVERAGE&&f->result_valid);
}
int main(void) {
    cw_flow_t f;
    // Full flow: watch-owned start, zero, lift and beverage; no Auto command at all.
    brew(&f);
    assert(f.result.water_cg==30000&&f.result.dose_cg==1500&&fabsf(f.ratio-20.f)<.0001f);
    // From the onset sample to the last settled sample before the lift: 92 steps.
    assert(f.result.elapsed_ms==9200);
    weights(&f,37000,9);assert(f.stage==CW_BEVERAGE);weights(&f,37000,2);
    assert(f.stage==CW_TDS&&f.beverage_cg==27000);
    // Final weighing is complete: the scale can power off before TDS arrives.
    cw_scale_link(&f,false,7);assert(f.stage==CW_TDS&&!f.interrupted&&f.beverage_valid&&f.result_valid);
    rs_event_t r={.kind=RS_MEASUREMENT,.tds_percent=1.4f};cw_rs_link(&f,true);
    cw_rs_message(&f,&r,f.wait_tds_us-1);assert(f.stage==CW_TDS);
    r.kind=RS_MEASUREMENT_ERROR;cw_rs_message(&f,&r,us+1);assert(f.stage==CW_TDS);
    r.kind=RS_MEASUREMENT;cw_rs_message(&f,&r,us+2);assert(f.stage==CW_REPORT);
    assert(fabsf(f.extraction-25.2f)<.0001f&&fabsf(f.retention-2.f)<.0001f);
    cw_home(&f);assert(!f.tds_valid&&!f.empty_valid&&!f.beverage_valid&&!f.scale.dose_valid);
    // Water already in when the probe reacts: the zero is the plateau before the pour
    // and the brew time starts at the weight onset, not at the trigger.
    prepare(&f);arm(&f);int64_t onset=pour(&f,8,9);
    assert(f.stage==CW_BREW&&f.zero_cg==40000&&f.onset_us==onset&&f.live_cg==40900);
    // A bump before the pour is skipped; the zero is the last plateau (>=1 s, <=0.2 g).
    prepare(&f);arm(&f);tick(&f,39750,20.f);tick(&f,41360,20.f);ticks(&f,40010,20.f,12);
    pour(&f,1,3);assert(f.stage==CW_BREW&&f.zero_cg==40010);
    // Water without a settled weight before it is never guessed.
    prepare(&f);arm(&f);
    for(unsigned i=0;i<60;i++)tick(&f,40000+(i%2?300:0),20.f);
    for(unsigned i=1;i<=20;i++)tick(&f,40000+(int32_t)i*100+(i%2?300:0),60.f);
    assert(f.interrupted&&!strcmp(f.error,"No stable weight before pour"));
    // A warm hand without water expires; the later real pour still starts.
    prepare(&f);arm(&f);ticks(&f,40000,24.f,46);assert(f.stage==CW_READY);
    pour(&f,1,3);assert(f.stage==CW_BREW&&f.zero_cg==40000);
    // Without the stored dose, preparation stays unarmed: a rinse never starts a brew,
    // and the trigger it left is cleared when the dose arrives and arms Ready.
    setup(&f);weights(&f,10000,51);assert(f.stage==CW_PREP);ticks(&f,40000,20.f,40);
    pour(&f,1,6);assert(f.stage==CW_PREP&&f.pour.triggered);
    bookoo_message_t rinse_dose={.kind=BOOKOO_DOSE,.dose_cg=1500};cw_scale_message(&f,7,&rinse_dose,us+1);
    arm(&f);assert(!f.pour.triggered);ticks(&f,40600,62.f,3);assert(f.stage==CW_READY);
    // The baseline restarts at arming, so the still-warm probe does not trigger either.
    ticks(&f,40600,61.f,40);assert(f.stage==CW_READY);pour(&f,1,4);assert(f.stage==CW_READY);
    // A cold probe reads as missing below the ADC floor; the floor is its baseline.
    prepare(&f);ticks(&f,40000,NAN,30);
    for(unsigned i=1;i<=4;i++)tick(&f,40000+(int32_t)i*100,i>1?40.f:NAN);
    assert(f.stage==CW_BREW&&f.zero_cg==40000);
    // ...but a lone reading without history is no baseline.
    prepare(&f);cw_pour_reset(&f.pour);arm(&f);pour(&f,0,3);assert(f.stage==CW_READY);
    // Scale Auto re-tares by itself, so any Auto event interrupts a watched stage.
    prepare(&f);arm(&f);event(&f,2);assert(f.interrupted&&!strcmp(f.error,"Turn off scale Auto"));
    brew(&f);event(&f,0);assert(f.interrupted);
    // Stirring, valve spikes and a transient press never end the brew.
    prepare(&f);arm(&f);pour(&f,1,4);assert(f.stage==CW_BREW);
    for(int32_t w=40500;w<70000;w+=500)tick(&f,w,90.f);
    ticks(&f,70000,88.f,20);
    for(unsigned i=0;i<30;i++)tick(&f,i%3==0?66000:(i%3==1?74000:70000),88.f);
    ticks(&f,62000,88.f,8);ticks(&f,70000,88.f,20);assert(f.stage==CW_BREW);
    ticks(&f,37000,80.f,11);assert(f.stage==CW_BEVERAGE&&f.result.water_cg==30000);
    // Sparse notifications cannot settle the lifted level.
    prepare(&f);arm(&f);pour(&f,1,4);ticks(&f,70000,88.f,20);
    weight(&f,37000,7);us+=1000001;weight(&f,37000,7);assert(f.stage==CW_BREW);
    ticks(&f,37000,80.f,11);assert(f.stage==CW_BEVERAGE);
    // Beverage needs the server on the scale: lifting everything waits.
    brew(&f);weights(&f,0,30);assert(f.stage==CW_BEVERAGE&&!f.beverage_valid);
    weights(&f,37000,11);assert(f.stage==CW_TDS&&f.beverage_cg==27000);
    // Back from Waiting TDS re-weighs the server, which never left the scale.
    cw_back(&f);assert(f.stage==CW_BEVERAGE&&!f.beverage_valid);weights(&f,37050,11);
    assert(f.stage==CW_TDS&&f.beverage_cg==27050);
    // Back revisits the frozen result; Continue returns to beverage weighing.
    brew(&f);int32_t frozen=f.result.water_cg;cw_back(&f);
    assert(f.stage==CW_BREW&&f.result_valid);ticks(&f,20000,80.f,20);assert(f.result.water_cg==frozen);
    cw_revisit_stopped(&f);assert(f.stage==CW_BEVERAGE&&f.result.water_cg==frozen);
    // Back acts at once: a live brew (a false start from a rinse) is discarded and
    // Ready re-arms; Back from Ready re-captures the empty server.
    prepare(&f);arm(&f);pour(&f,1,4);assert(f.stage==CW_BREW);cw_back(&f);
    assert(f.stage==CW_READY&&!f.result_valid&&f.scale.dose_valid&&f.empty_valid);
    ticks(&f,40000,30.f,40);pour(&f,1,4);assert(f.stage==CW_BREW);cw_back(&f);
    assert(f.stage==CW_READY);cw_back(&f);assert(f.stage==CW_EMPTY&&!f.empty_valid);
    // Missing current dose never gets inferred: preparation stays unarmed.
    setup(&f);weights(&f,10000,51);assert(f.stage==CW_PREP);ticks(&f,40000,20.f,40);
    pour(&f,1,10);assert(f.stage==CW_PREP);
    // Interrupted brewing rejects a new RS result. Explicit Home clears evidence.
    brew(&f);cw_scale_link(&f,false,7);assert(f.interrupted&&f.result_valid);
    cw_rs_link(&f,true);cw_rs_message(&f,&r,us+1);assert(!f.tds_valid);cw_home(&f);assert(!f.result_valid);
    // A stored dose received before choosing a brewer is retained throughout navigation.
    cw_init(&f);cw_scale_link(&f,true,7);
    bookoo_message_t dose={.kind=BOOKOO_DOSE,.dose_cg=1820};cw_scale_message(&f,7,&dose,us+1);
    cw_choose(&f,2);assert(f.scale.dose_valid&&f.scale.dose_cg==1820);
    initialize_mode(&f,2);weights(&f,10000,51);
    arm(&f);cw_back(&f);assert(f.stage==CW_EMPTY);cw_home(&f);assert(f.scale.dose_valid&&f.scale.dose_cg==1820);
    // Manual Ratio submode: tare only; other modes cannot provide the stored dose.
    cw_init(&f);cw_scale_link(&f,true,7);cw_choose(&f,0);initialize_mode(&f,1);
    weights(&f,10000,51);assert(f.stage==CW_PREP);
    uint8_t p[6];cw_init(&f);cw_scale_link(&f,true,7);cw_choose(&f,0);
    assert(cw_initial_request(&f,p));cw_write_done(&f,true);
    bookoo_message_t wrong_mode={.kind=BOOKOO_MODE,.mode=1,.submode=0};
    cw_scale_message(&f,7,&wrong_mode,us+1);assert(f.interrupted&&!cw_initial_request(&f,p));
    cw_home(&f);cw_choose(&f,0);assert(cw_initial_request(&f,p));
    cw_write_done(&f,false);assert(f.interrupted&&!cw_initial_request(&f,p));
    weights(&f,20000,60);assert(f.stage==CW_EMPTY&&!f.empty_valid);
    // Reset in empty-server capture is a plain tare dispatch, never an ACK.
    setup(&f);assert(cw_reset_dispatch(&f,p)&&p[2]==1&&p[5]==8&&f.write_pending);cw_write_done(&f,true);
    // No stale epoch, <=10g, or sparse packet sequence may complete capture.
    setup(&f);for(unsigned i=0;i<60;i++)weight(&f,20000,6);assert(!f.live_valid);
    weights(&f,1000,60);assert(f.stage==CW_EMPTY&&!f.stable.active);
    weight(&f,20000,7);us+=5000000;weight(&f,20000,7);assert(f.stage==CW_EMPTY&&f.stable.progress==0);
    // Inclusive .2g tolerance, and drift restarting the primary stability window.
    setup(&f);weights(&f,10000,25);weight(&f,10025,7);assert(f.stable.progress==0);
    weights(&f,10025,25);assert(f.stage==CW_EMPTY);weights(&f,10025,25);assert(f.stage==CW_PREP);
    assert(cw_mass_cg(19641)==19640&&cw_mass_cg(19645)==19650);
    assert(cw_mass_cg(-19645)==-19650&&cw_mass_cg(-4)==0);
    // Switch and AeroPress steep, then drain or press out completely: extraction
    // counts all added water (immersion), while retention still uses the beverage.
    for(unsigned i=0;i<2;i++) {
        brewer=i?CW_AEROPRESS:CW_SWITCH;brew(&f);assert(f.brewer==brewer);
        weights(&f,37000,11);assert(f.stage==CW_TDS&&f.beverage_cg==27000&&f.result.water_cg==30000);
        cw_rs_link(&f,true);r.kind=RS_MEASUREMENT;cw_rs_message(&f,&r,us+1);assert(f.stage==CW_REPORT);
        assert(fabsf(f.extraction-1.4f/98.6f*20*100)<.001f&&fabsf(f.retention-2.f)<.0001f&&fabsf(f.ratio-20.f)<.0001f);
    }
    assert(cw_brewer_immersion(CW_SWITCH)&&cw_brewer_immersion(CW_AEROPRESS)&&!cw_brewer_immersion(CW_V60)&&!cw_brewer_immersion(CW_HOOP));
    brewer=CW_HOOP;brew(&f);cw_back(&f);cw_back(&f);cw_back(&f);cw_back(&f);
    assert(f.stage==CW_BREWER&&f.brewer==CW_HOOP);cw_home(&f);assert(f.brewer==CW_HOOP);
    puts("20 full-flow scenarios passed");
}
