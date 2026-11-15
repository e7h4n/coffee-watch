// SPDX-License-Identifier: MIT
#include "color_mode.h"
#include "color_sensor.h"
#include "roast_color.h"
#include "design.h"
#include "esp_log.h"
#include "nvs.h"
#include "dropbox_setup.h"
#include <cstdio>
#include <cstring>
static const char *TAG="color";
static rc_meter_t meter;
static rc_cal_t cal;
static bool calibrating,streaming;
static uint8_t pa=RC_DEFAULT_PA;
static bool calibrated;
static float soda;
static int64_t start_us,last_sample_us;
static ColorView view;
static UiState *held_ui;
// Dropbox: one record per group, named <session>-<sequence> like brew records.
static char session[40],group_id[64];
static unsigned group_sequence;
static cw_record_status_t upload_state;
static bool upload_known;
// The calibrated IR amplitude and its baking-soda reading survive reboots.
static void load_settings() {
    nvs_handle_t h;uint8_t level=0;uint16_t soda_x10=0;
    if(nvs_open("cw_color",NVS_READONLY,&h)==ESP_OK) {nvs_get_u8(h,"ir_pa",&level);nvs_get_u16(h,"soda_x10",&soda_x10);nvs_close(h);}
    if(level)pa=level;
    calibrated=level&&soda_x10;soda=soda_x10/10.f;
}
static void save_settings() {
    nvs_handle_t h;
    if(nvs_open("cw_color",NVS_READWRITE,&h)!=ESP_OK) {ESP_LOGW(TAG,"calibration not saved");return;}
    uint16_t soda_x10=(uint16_t)(soda*10+.5f);
    if(nvs_set_u8(h,"ir_pa",pa)!=ESP_OK||nvs_set_u16(h,"soda_x10",soda_x10)!=ESP_OK||nvs_commit(h)!=ESP_OK)ESP_LOGW(TAG,"calibration not saved");
    nvs_close(h);
}
// Leaving calibration measures from a fresh state; the LED warm-up is not repeated.
static void resume_measuring() {calibrating=false;color_sensor_set_ir(pa);rc_meter_init(&meter,start_us);}
static ColorPage page_for(int64_t now) {
    if(calibrating)switch(cal.phase) {
        case RC_CAL_PLACE:return COLOR_CAL_PLACE;
        case RC_CAL_SETTLE:return COLOR_CAL_SETTLE;
        case RC_CAL_STEP:return COLOR_CAL_STEP;
        case RC_CAL_DONE:return COLOR_CAL_DONE;
        case RC_CAL_FAILED:return COLOR_CAL_FAILED;
    }
    if(now-last_sample_us>1500000)return COLOR_NO_DATA;
    switch(rc_meter_phase(&meter,now)) {
    case RC_WARMUP:return COLOR_WARMUP;
    case RC_PLACE:return COLOR_PLACE;
    case RC_MEASURING:return COLOR_MEASURING;
    case RC_READING:return COLOR_READING;
    case RC_RESULT:return COLOR_RESULT;
    case RC_SATURATED:return COLOR_SATURATED;
    }
    return COLOR_PLACE;
}
static void build_view(int64_t now) {
    ColorView v;
    v.page=page_for(now);
    v.live_valid=meter.have_raw&&meter.loaded;v.live=rc_index((float)meter.raw);
    v.progress=meter.window.progress;
    v.count=meter.count;for(unsigned i=0;i<RC_GROUP;i++)v.readings[i]=meter.readings[i];
    v.mean=meter.mean;v.sd=meter.sd;v.retest=meter.retest;
    v.warmup_ms=meter.ready_us>now?(uint32_t)((meter.ready_us-now)/1000):0;
    v.calibrated=calibrated;v.soda=soda;v.pa=pa;
    bool group_page=v.page==COLOR_PLACE||v.page==COLOR_MEASURING||v.page==COLOR_READING||v.page==COLOR_RESULT;
    if(group_page&&meter.count&&upload_known&&!strcmp(upload_state.id,group_id)) {
        v.sync_visible=true;v.sync=upload_state.state;v.sync_unconfigured=upload_state.state==CW_RECORD_FAILED&&!strcmp(upload_state.error,"authorize_first");
    }
    if(calibrating) {
        v.progress=cal.phase==RC_CAL_SETTLE?cal.window.progress:rc_cal_progress(&cal);
        v.pa=cal.phase==RC_CAL_FAILED?cal.original:cal.pa;
        v.failure=cal.error==RC_CAL_TOO_DARK?COLOR_FAIL_TOO_DARK:COLOR_FAIL_MOVED;v.cal_index=cal.index;
    }
    view=v;
}
static void action() {
    bool enabled;ui_color_action(view,&enabled);
    if(!enabled)return;
    switch(view.page) {
    case COLOR_CAL_PLACE:case COLOR_CAL_SETTLE:case COLOR_CAL_STEP:
        ESP_LOGI(TAG,"calibration canceled; LED %u kept",pa);resume_measuring();break;
    case COLOR_CAL_DONE:case COLOR_CAL_FAILED:resume_measuring();break;
    default:calibrating=true;rc_cal_start(&cal,pa);ESP_LOGI(TAG,"calibration started at LED %u",pa);break;
    }
}
// Back clears the current group; during calibration it cancels like the bottom action.
static void back() {
    if(calibrating) {if(cal.phase!=RC_CAL_DONE&&cal.phase!=RC_CAL_FAILED)ESP_LOGI(TAG,"calibration canceled; LED %u kept",pa);resume_measuring();}
    else rc_meter_clear(&meter);
}
// Every reading uploads the whole group; the first reading of a group names it.
static void upload_group() {
    if(meter.count==1)snprintf(group_id,sizeof group_id,"%s-%08x",session,++group_sequence);
    rc_record_t record;rc_record_from_meter(&record,&meter,group_id,pa,calibrated,soda);
    upload_state={};upload_state.state=CW_RECORD_SAVING;upload_state.revision=record.revision;snprintf(upload_state.id,sizeof upload_state.id,"%s",group_id);upload_known=true;
    dropbox_setup_color(&record);
}
static void log_events(unsigned events) {
    if(events&RC_EV_READING)ESP_LOGI(TAG,"READING %u/3 index=%.1f raw=%lu led=%u",meter.count,(double)meter.readings[meter.count-1],(unsigned long)meter.raw,pa);
    if(events&RC_EV_RESULT)ESP_LOGI(TAG,"RESULT mean=%.1f sd=%.2f readings=%.1f,%.1f,%.1f led=%u soda=%.1f retest=%u",(double)meter.mean,(double)meter.sd,
        (double)meter.readings[0],(double)meter.readings[1],(double)meter.readings[2],pa,(double)soda,meter.retest);
}
static void feed(const color_sample_t &s) {
    last_sample_us=s.us;
    if(streaming)printf("IR t_ms=%lld raw=%lu led=%u\n",(long long)(s.us/1000),(unsigned long)s.raw,s.pa);
    if(!calibrating) {
        if(s.pa!=pa)return;
        unsigned events=rc_meter_sample(&meter,s.raw,s.us);log_events(events);
        if(events&RC_EV_READING)upload_group();
        return;
    }
    rc_cal_phase_t before=cal.phase;uint8_t level=0;
    if(rc_cal_sample(&cal,s.raw,s.pa,s.us,&level))color_sensor_set_ir(level);
    if(cal.phase==before)return;
    if(cal.phase==RC_CAL_DONE) {
        ESP_LOGI(TAG,"CALIBRATED led=%u soda=%.1f previous_led=%u steps=%u",cal.chosen,(double)cal.index,cal.original,cal.steps);
        pa=cal.chosen;soda=cal.index;calibrated=true;save_settings();
    } else if(cal.phase==RC_CAL_FAILED)ESP_LOGW(TAG,"calibration failed (%s); LED %u kept",cal.error==RC_CAL_TOO_DARK?"too dark":"moved",pa);
}
struct Keys {
    bool next=false,select=false,next_raw=false,select_raw=false,touch=false;
    bool next_consumed=false,select_consumed=false,chord=false,touch_cancel=false;
    int touch_action=0;ColorPage select_page=COLOR_WARMUP,touch_page=COLOR_WARMUP;
    int64_t next_change=0,select_change=0,next_start=0,chord_start=0;
};
static Keys keys;
// Same gestures as the brew assistant: release executes, dragging out or a page
// change cancels, Next held 650 ms is Back, both keys held 900 ms request Home.
static bool input_tick(BoardInput raw,int64_t now) {
    bool home=false;
    if(!raw.touch_valid) {keys.touch_cancel=true;raw.touch=false;}
    bool prev_next=keys.next,prev_select=keys.select;
    if(raw.next!=keys.next_raw) {keys.next_raw=raw.next;keys.next_change=now;}
    if(raw.select!=keys.select_raw) {keys.select_raw=raw.select;keys.select_change=now;}
    if(now-keys.next_change>=15000)keys.next=keys.next_raw;
    if(now-keys.select_change>=15000)keys.select=keys.select_raw;
    if(keys.next&&!prev_next) {keys.next_start=now;keys.next_consumed=false;}
    if(keys.select&&!prev_select) {keys.select_consumed=false;keys.select_page=view.page;}
    if(keys.next&&keys.select&&!keys.chord) {keys.chord=true;keys.chord_start=now;keys.next_consumed=keys.select_consumed=true;}
    if(keys.select&&keys.select_page!=view.page)keys.select_consumed=true;
    if(keys.chord) {
        if(keys.chord_start&&keys.next&&keys.select&&now-keys.chord_start>=900000) {home=true;keys.chord_start=0;}
        if(!keys.next&&!keys.select)keys.chord=false;
    } else {
        if(keys.next&&!keys.next_consumed&&now-keys.next_start>=650000) {keys.next_consumed=keys.select_consumed=true;back();}
        if(!keys.select&&prev_select&&!keys.select_consumed)action();
    }
    if(raw.touch&&!keys.touch) {keys.touch_action=ui_color_hit(view,raw.x,raw.y);keys.touch_cancel=false;keys.touch_page=view.page;}
    if(raw.touch&&(ui_color_hit(view,raw.x,raw.y)!=keys.touch_action||keys.touch_page!=view.page))keys.touch_cancel=true;
    if(!raw.touch&&keys.touch&&keys.touch_action&&!keys.touch_cancel)action();
    keys.touch=raw.touch;
    bool enabled;ui_color_action(view,&enabled);
    held_ui->held=enabled&&!keys.chord&&((keys.select&&!keys.select_consumed)||(keys.touch&&keys.touch_action&&!keys.touch_cancel))?1:0;
    return home;
}
static void print_status(int64_t now) {
    printf("COLOR page=%d raw=%lu index=%.1f loaded=%u count=%u mean=%.1f sd=%.2f led=%u calibrated=%u soda=%.1f warmup_ms=%lu errors=%u last_sample_ms=%lld\n",
        (int)view.page,(unsigned long)meter.raw,(double)rc_index((float)meter.raw),meter.loaded,meter.count,(double)meter.mean,(double)meter.sd,pa,calibrated,(double)soda,
        (unsigned long)view.warmup_ms,color_sensor_errors(),(long long)((now-last_sample_us)/1000));
}
void color_mode_init(const char *id_session) {snprintf(session,sizeof session,"%s",id_session);}
void color_mode_enter(int64_t now) {
    load_settings();
    start_us=last_sample_us=now;calibrating=false;keys=Keys();view=ColorView();group_id[0]=0;upload_known=false;
    rc_meter_init(&meter,start_us);color_sensor_start(pa);
    ESP_LOGI(TAG,"roast color mode: LED %u, %s",pa,calibrated?"calibrated":"not calibrated");
    if(calibrated)ESP_LOGI(TAG,"baking soda %.1f",(double)soda);
}
void color_mode_leave() {
    if(calibrating&&cal.phase!=RC_CAL_DONE&&cal.phase!=RC_CAL_FAILED)ESP_LOGI(TAG,"calibration canceled; LED %u kept",pa);
    calibrating=false;streaming=false;color_sensor_stop();ESP_LOGI(TAG,"left roast color mode");
}
bool color_mode_tick(int64_t now) {
    bool changed=false;color_sample_t s;unsigned n=0;
    while(n++<16&&color_sensor_next(&s)) {
        unsigned count=meter.count;bool loaded=meter.loaded;rc_cal_phase_t phase=cal.phase;
        feed(s);
        changed|=meter.count!=count||meter.loaded!=loaded||cal.phase!=phase;
    }
    ColorPage before=view.page;build_view(now);
    return changed||view.page!=before;
}
bool color_mode_input(BoardInput raw,int64_t now,UiState &ui) {held_ui=&ui;bool home=input_tick(raw,now);build_view(now);return home;}
// About a second of failed reads, or no sample for 3 s, means the sensor is gone.
bool color_mode_sensor_lost(int64_t now) {return color_sensor_errors()>=25||now-last_sample_us>3000000;}
void color_mode_record(const cw_record_status_t &status) {
    if(group_id[0]&&!strcmp(status.id,group_id)&&status.revision>=upload_state.revision) {upload_state=status;upload_known=true;}
}
bool color_mode_console(const char *line,int64_t now) {
    if(!strcmp(line,"status")||!strcmp(line,"color"))print_status(now);
    else if(!strcmp(line,"stream")) {streaming=!streaming;printf("STREAM %s\n",streaming?"on":"off");}
    else if(!strcmp(line,"select")||!strcmp(line,"calibrate"))action();
    else if(!strcmp(line,"back"))back();
    else return false;
    build_view(now);return true;
}
bool color_mode_animating(int64_t now) {return ui_color_tick(view,now);}
void color_mode_draw(const UiState &ui) {ui_color_draw(view,ui);}
