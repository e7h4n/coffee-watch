#include "coffee_flow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int64_t us;
static void weight(cw_flow_t *f,int32_t value,unsigned epoch) {
    bookoo_message_t m={.kind=BOOKOO_WEIGHT,.weight_cg=value};us+=100000;cw_scale_message(f,epoch,&m,us);
}
static void weights(cw_flow_t *f,int32_t value,unsigned count) {for(unsigned i=0;i<count;i++)weight(f,value,7);}
static void event(cw_flow_t *f,uint8_t state,int32_t value,uint32_t ms) {
    bookoo_message_t m={.kind=BOOKOO_AUTO,.state=state,.weight_cg=value,.elapsed_ms=ms};us+=100000;cw_scale_message(f,7,&m,us);
}
static void setup(cw_flow_t *f) {cw_init(f);cw_scale_link(f,true,7);assert(f->stage==CW_BREWER);cw_choose(f,0);assert(f->stage==CW_EMPTY);}
static void prepare(cw_flow_t *f) {
    setup(f);weights(f,10000,51);assert(f->stage==CW_PREP&&f->empty_cg==10000);
    bookoo_message_t m={.kind=BOOKOO_DOSE,.dose_cg=1500};cw_scale_message(f,7,&m,us+1);assert(f->scale.dose_valid);
}
static void stop(cw_flow_t *f) {
    prepare(f);uint8_t p[6];assert(cw_ready_request(f,p));assert(p[2]==0x18&&p[3]==0&&p[5]==0x11);assert(f->stage==CW_PREP);
    cw_write_done(f,true);assert(f->stage==CW_PREP);event(f,2,0,5000);assert(f->stage==CW_READY);
    event(f,1,0,5000);assert(f->stage==CW_BREW);
    weights(f,30000,2);assert(cw_timer_request(f,BOOKOO_CMD_STOP,p));assert(f->stage==CW_BREW);
    cw_write_done(f,true);assert(f->stage==CW_BREW);event(f,0,30000,60000);assert(f->stage==CW_BEVERAGE);
}
int main(void) {
    cw_flow_t f;uint8_t reset_packet[6];stop(&f);
    weights(&f,-10000,60);assert(f.stage==CW_BEVERAGE&&f.scale.result.water_cg==30000);
    assert(!cw_reset_dispatch(&f,reset_packet));
    weights(&f,0,60);assert(f.stage==CW_BEVERAGE&&!f.beverage_valid);
    weights(&f,-37000,10);assert(f.stage==CW_BEVERAGE);
    weights(&f,-37000,1);assert(f.stage==CW_TDS&&f.beverage_cg==27000);
    rs_event_t r={.kind=RS_MEASUREMENT,.tds_percent=1.4f};cw_rs_link(&f,true);
    cw_rs_message(&f,&r,f.wait_tds_us-1);assert(f.stage==CW_TDS);
    r.kind=RS_MEASUREMENT_ERROR;cw_rs_message(&f,&r,us+1);assert(f.stage==CW_TDS);
    r.kind=RS_MEASUREMENT;cw_rs_message(&f,&r,us+2);assert(f.stage==CW_REPORT);
    assert(fabsf(f.extraction-25.2f)<.0001f&&fabsf(f.retention-2.f)<.0001f&&fabsf(f.ratio-20.f)<.0001f);
    weights(&f,0,2);assert(f.beverage_cg==27000&&f.scale.result.water_cg==30000);
    cw_home(&f);assert(!f.tds_valid&&!f.empty_valid&&!f.beverage_valid&&f.scale.dose_valid);
    // No stale epoch, <=10g, or sparse packet sequence may complete capture.
    setup(&f);for(unsigned i=0;i<60;i++)weight(&f,20000,6);assert(!f.live_valid);
    weights(&f,1000,60);assert(f.stage==CW_EMPTY&&!f.stable.active);
    weight(&f,20000,7);us+=5000000;weight(&f,20000,7);assert(f.stage==CW_EMPTY&&f.stable.progress==0);
    // Inclusive .2g tolerance, and drift restarting the primary stability window.
    setup(&f);weights(&f,10000,25);weight(&f,10025,7);assert(f.stable.progress==0);
    weights(&f,10025,25);assert(f.stage==CW_EMPTY);weights(&f,10025,25);assert(f.stage==CW_PREP);
    // An ATT reply does not substitute for Start or Stop.
    prepare(&f);uint8_t p[6];assert(cw_ready_request(&f,p));cw_write_done(&f,true);
    assert(f.stage==CW_PREP&&f.scale.pending==BOOKOO_CMD_READY);
    event(&f,2,0,0);assert(f.stage==CW_READY);event(&f,1,0,0);assert(f.stage==CW_BREW);
    // Missing current dose never gets inferred from weight or ratio.
    setup(&f);weights(&f,10000,51);assert(!cw_ready_request(&f,p));event(&f,2,0,0);event(&f,1,0,0);assert(f.interrupted);
    // Interrupted brewing rejects a new RS result. Explicit Home clears evidence.
    stop(&f);cw_scale_link(&f,false,7);assert(f.interrupted&&f.scale.result_valid);
    cw_rs_link(&f,true);cw_rs_message(&f,&r,us+1);assert(!f.tds_valid);cw_home(&f);assert(!f.scale.result_valid);
    // Positive weight and sparse negative packets cannot complete removal.
    stop(&f);weights(&f,37000,60);assert(f.stage==CW_BEVERAGE);
    weight(&f,-37000,7);us+=1000001;weight(&f,-37000,7);
    assert(f.stage==CW_BEVERAGE&&f.stable.progress==0);
    weights(&f,-37000,10);assert(f.stage==CW_TDS);
    // Actual clear-water observation: -336.1g minus 298.0g empty server.
    stop(&f);f.empty_cg=29800;weights(&f,-33610,11);
    assert(f.stage==CW_TDS&&f.beverage_cg==3810);
    // Back revisits frozen Stop evidence; it never dispatches another Stop.
    stop(&f);int32_t frozen=f.scale.result.water_cg;cw_back(&f);
    assert(f.stage==CW_BREW&&f.scale.phase==BOOKOO_STOPPED);
    weights(&f,0,2);assert(f.scale.result.water_cg==frozen);
    cw_revisit_stopped(&f);assert(f.stage==CW_BEVERAGE&&f.scale.result.water_cg==frozen);
    // Back during a live brew waits for the one real Stop event before prep.
    prepare(&f);event(&f,2,0,0);event(&f,1,0,0);cw_back(&f);assert(f.stage==CW_BREW&&f.back_pending);
    assert(cw_timer_request(&f,BOOKOO_CMD_STOP,p));cw_write_done(&f,true);
    assert(f.stage==CW_BREW);event(&f,0,1000,5000);
    assert(f.stage==CW_PREP&&f.scale.dose_valid&&!f.back_pending);
    cw_back(&f);assert(f.stage==CW_EMPTY&&!f.empty_valid);
    // A stored dose received before choosing a brewer is retained throughout
    // navigation; Ready is a separate device event, never manual timer Start.
    cw_init(&f);cw_scale_link(&f,true,7);
    bookoo_message_t dose={.kind=BOOKOO_DOSE,.dose_cg=1820};cw_scale_message(&f,7,&dose,us+1);
    cw_choose(&f,2);assert(f.scale.dose_valid&&f.scale.dose_cg==1820);
    weights(&f,10000,51);assert(f.stage==CW_PREP);
    assert(!cw_timer_request(&f,BOOKOO_CMD_START,p));assert(!cw_reset_dispatch(&f,reset_packet));
    assert(cw_ready_request(&f,p));cw_write_done(&f,true);assert(f.stage==CW_PREP);
    weights(&f,0,10);assert(f.stage==CW_PREP);
    event(&f,2,0,0);assert(f.stage==CW_READY&&!f.scale.pending&&!f.write_pending);
    cw_back(&f);assert(f.stage==CW_READY&&cw_exit_ready_request(&f,p));
    assert(p[2]==0x18&&p[3]==1&&p[5]==0x10);cw_write_done(&f,true);
    assert(f.stage==CW_READY);event(&f,3,0,0);assert(f.stage==CW_PREP&&f.scale.dose_valid);
    cw_home(&f);assert(f.scale.dose_valid&&f.scale.dose_cg==1820);
    cw_choose(&f,1);assert(f.scale.dose_valid&&f.scale.dose_cg==1820);
    assert(cw_mass_cg(19641)==19640&&cw_mass_cg(19645)==19650);
    assert(cw_mass_cg(-19645)==-19650&&cw_mass_cg(-4)==0);
    puts("10 full-flow scenarios passed");
}
