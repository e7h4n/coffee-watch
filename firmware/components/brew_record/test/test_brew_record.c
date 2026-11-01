// SPDX-License-Identifier: MIT
#include "brew_record.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
int main(void) {
    cw_record_tracker_t t;cw_record_init(&t,"device-session");cw_flow_t f={0};cw_brew_record_t draft,final,again;
    f.stage=CW_BREW;assert(!cw_record_event(&t,CW_READY,&f,100,NULL));
    f.stage=CW_BEVERAGE;assert(!cw_record_event(&t,CW_BREW,&f,100,NULL));
    f.stage=CW_TDS;f.beverage_valid=true;f.scale.result_valid=true;f.scale.result.dose_valid=true;f.scale.result.water_valid=true;
    f.scale.result.dose_cg=1504;f.scale.result.water_cg=22504;f.beverage_cg=19645;f.empty_cg=29804;f.scale.result.elapsed_ms=180000;f.brewer=1;
    assert(cw_record_event(&t,CW_BEVERAGE,&f,200,&draft));assert(!draft.complete&&draft.revision==1&&draft.beverage_cg==19650);
    assert(!cw_record_event(&t,CW_TDS,&f,300,&again));
    char json[1536];assert(cw_record_json(&draft,"2026-11-02T08:00:00Z",json,sizeof json)>0);
    assert(strstr(json,"\"beverage_g\":196.5"));assert(strstr(json,"\"dose_g\":15.0"));assert(strstr(json,"\"tds_percent\":null"));assert(strstr(json,"\"status\":\"awaiting_tds\""));puts(json);
    f.stage=CW_REPORT;f.tds_valid=true;f.tds=.9;f.temperature=22.8;f.refraction=1.333;f.extraction=11.79;
    assert(cw_record_event(&t,CW_TDS,&f,400,&final));assert(final.complete&&final.revision==2&&!strcmp(final.id,draft.id)&&final.captured_us==draft.captured_us);
    assert(!cw_record_event(&t,CW_REPORT,&f,500,&again));
    assert(cw_record_json(&final,"2026-11-02T08:00:00Z",json,sizeof json)>0);assert(strstr(json,"\"tds_percent\":0.90"));assert(strstr(json,"\"status\":\"complete\""));puts(json);
    assert(!draft.complete&&draft.revision==1); // Queued snapshots are immutable.
    f.stage=CW_TDS;f.tds_valid=false;f.beverage_cg=20000;
    assert(cw_record_event(&t,CW_BEVERAGE,&f,600,&again));assert(again.revision==3&&!strcmp(again.id,draft.id)&&!again.complete);
    f.stage=CW_BREW;assert(!cw_record_event(&t,CW_READY,&f,700,NULL));f.stage=CW_TDS;
    assert(cw_record_event(&t,CW_BEVERAGE,&f,800,&again));assert(strcmp(again.id,draft.id)&&again.revision==1);
    f.interrupted=true;assert(!cw_record_event(&t,CW_BEVERAGE,&f,900,&again));
    final.tds=0;final.extraction=0;assert(cw_record_json(&final,"2026-11-02T08:00:00Z",json,sizeof json)>0);assert(strstr(json,"\"tds_percent\":0.00"));
    final.tds=NAN;assert(cw_record_json(&final,"2026-11-02T08:00:00Z",json,sizeof json)<0);
    assert(cw_record_json(&draft,"not UTC",json,sizeof json)<0);assert(cw_record_json(&draft,"2026-11-02T08:00:00Z",json,8)<0);
    draft.dose_cg=1;assert(cw_record_json(&draft,"2026-11-02T08:00:00Z",json,sizeof json)<0);
    return 0;
}
