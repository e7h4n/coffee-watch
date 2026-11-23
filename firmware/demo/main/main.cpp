// SPDX-License-Identifier: MIT
#include "ui.h"
#include "audio.h"
#include "brew_backup.h"
#include "design.h"
#include "probe_trace.h"
#include "color_mode.h"
#include "color_sensor.h"
#include "app_mode.h"
#include "launcher_app.h"
#include "firmware_update.h"
#include "update_policy.h"
#include "esp_app_desc.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <sys/time.h>
static QueueHandle_t events,console_commands;
static cw_flow_t flow;
static UiState ui;
// Home picks the product: brew assistant (scale) or roast colorimeter (MAX30101).
static cw_app_t app;
static unsigned rs_epoch;
static cw_record_tracker_t records;
static bool render_needed=true,rs_requested;
static int64_t network_activity_us;
static unsigned slow_drip_seen;
static int64_t animation_start;
static int queued[16];static unsigned queue_read,queue_write;
// Main owns the update modal. A separate worker owns downloads; do not release
// Dropbox admission or leave this modal until that worker acknowledges idle.
static int64_t now_us();
static bool firmware_lock=false,firmware_close_when_idle=false;
static int64_t firmware_last_poll;
static void firmware_notice(const char *message) {
    snprintf(ui.firmware_notice,sizeof ui.firmware_notice,"%s",message);render_needed=true;
}
static void firmware_leave() {
    if(firmware_lock)dropbox_setup_maintenance_end();
    firmware_lock=false;firmware_close_when_idle=false;ui.firmware_open=false;ui.held=0;render_needed=true;
}
static void firmware_sync(int64_t now,bool force=false) {
    if(!ui.firmware_open||(!force&&now-firmware_last_poll<100000))return;
    firmware_last_poll=now;firmware_update_status_t status={};
    if(firmware_update_get_status(&status)!=ESP_OK)return;
    auto &old=ui.firmware_update;
    if(status.state!=old.state||status.busy!=old.busy||status.received_bytes!=old.received_bytes||status.total_bytes!=old.total_bytes||
       strcmp(status.version,old.version)||strcmp(status.message,old.message))render_needed=true;
    old=status;
#if CONFIG_CW_LAUNCHER_APP
    bool enabled=false;
#else
    bool enabled=!firmware_close_when_idle;
#endif
    if(enabled!=ui.firmware_primary_enabled) {ui.firmware_primary_enabled=enabled;render_needed=true;}
    if(status.busy&&cw_update_must_cancel_for_power(ui.battery_percent)) {
        firmware_update_cancel();firmware_notice("Battery low; stopping update");
    }
    if(firmware_close_when_idle&&!status.busy)firmware_leave();
}
static void firmware_close() {
    firmware_update_cancel();firmware_close_when_idle=true;firmware_sync(now_us(),true);
}
static bool firmware_admit() {
    if(firmware_lock)return true;
    if(!cw_update_idle_allowed(app.mode==CW_APP_HOME,flow.stage==CW_HOME||flow.stage==CW_BREWER,flow.interrupted,ui.wifi_setup)) {
        firmware_notice("Return Home after finishing brew");return false;
    }
    if(!dropbox_setup_maintenance_begin()) {firmware_notice("Finish or save pending uploads");return false;}
    firmware_lock=true;return true;
}
static void firmware_request(bool download) {
    if(!firmware_admit())return;
    if(ui.network.state!=CW_NET_CONNECTED) {firmware_notice("Connect Wi-Fi from Home first");return;}
    if(download&&!cw_update_power_allowed(board_battery_percent())) {firmware_notice("Charge battery to at least 30%");return;}
    ui.firmware_notice[0]=0;
    esp_err_t err=download?firmware_update_request_download():firmware_update_request_check();
    if(err!=ESP_OK)render_needed=true; // Worker status carries the specific refusal.
    firmware_sync(now_us(),true);
}
static void firmware_open() {
    if(ui.firmware_open)return;
    if(!cw_update_idle_allowed(app.mode==CW_APP_HOME,flow.stage==CW_HOME||flow.stage==CW_BREWER,flow.interrupted,ui.wifi_setup))return;
    ui.firmware_open=true;ui.held=0;firmware_close_when_idle=false;
    snprintf(ui.firmware_current,sizeof ui.firmware_current,"%s",esp_app_get_description()->version);
#if !CONFIG_CW_LAUNCHER_APP
    firmware_request(false);
#endif
    render_needed=true;
}
static void firmware_action(int which) {
    if(which==UI_FIRMWARE_BACK) {firmware_close();return;}
    if(which!=1||firmware_close_when_idle)return;
    firmware_sync(now_us(),true);
    if(ui.firmware_update.busy) {firmware_update_cancel();firmware_notice("Stopping update; please wait");return;}
    if(ui.firmware_update.state==FIRMWARE_UPDATE_READY) {
        if(!firmware_admit())return;
        if(!cw_update_power_allowed(board_battery_percent())) {firmware_notice("Charge battery to at least 30%");return;}
        esp_err_t err=firmware_update_activate_and_restart(firmware_lock);
        if(err!=ESP_OK)firmware_notice("Install refused; check update again");
    } else firmware_request(ui.firmware_update.state==FIRMWARE_UPDATE_AVAILABLE);
}
extern "C" void demo_post(const demo_event_t *event) {configASSERT(xQueueSend(events,event,0)==pdTRUE);}
extern "C" void demo_record_status(const cw_record_status_t *status) {demo_event_t e={};e.kind=DE_RECORD;e.us=esp_timer_get_time();e.data.record=*status;demo_post(&e);}
static int64_t now_us() {return esp_timer_get_time();}
// The last confirmed brewer is the carousel's starting position after a reboot.
static unsigned saved_brewer() {
    nvs_handle_t h;uint8_t v=0;
    if(nvs_open("cw_ui",NVS_READONLY,&h)==ESP_OK) {nvs_get_u8(h,"brewer",&v);nvs_close(h);}
    return v%4;
}
static void save_brewer(unsigned brewer) {
    if(brewer==saved_brewer())return;
    nvs_handle_t h;
    if(nvs_open("cw_ui",NVS_READWRITE,&h)!=ESP_OK) {ESP_LOGW("flow","brewer not saved");return;}
    if(nvs_set_u8(h,"brewer",(uint8_t)brewer)!=ESP_OK||nvs_commit(h)!=ESP_OK)ESP_LOGW("flow","brewer not saved");
    nvs_close(h);
}
// Result pages advance every 5 seconds; a manual turn restarts that interval.
// Turning is display-only, so it never counts as activity for the idle shutdown.
static constexpr int64_t result_page_us=5000000;
static void turn_page(int dir,int64_t now) {
    int n=ui_result_pages(flow);
    if(n<2||ui.wifi_setup||ui.firmware_open)return;
    ui.result_page=(ui.result_page+dir+n)%n;ui.page_us=now;render_needed=true;
}
// Brew trace: record from Ready through brewing; stop when the brew stops.
// The brew mark is the detected pour onset, which precedes the trigger slightly.
static cw_stage_t trace_stage=CW_HOME;
static unsigned trace_total;
// Two scales: the trace keeps both raw weights with the brew's zeros and roles; a brew
// that ended too early (more water poured before the TDS) continues the same trace.
static void trace_follow(int64_t us) {
    if(flow.stage==trace_stage) {
        if(flow.dual&&flow.stage==CW_BREW&&flow.duo.total!=trace_total) {trace_total=flow.duo.total;probe_trace_roles(trace_total);}
        return;
    }
    if(flow.stage==CW_READY)probe_trace_start(us);
    else if(flow.stage==CW_BREW&&trace_stage==CW_READY) {
        if(flow.dual) {trace_total=flow.duo.total;probe_trace_brew_dual(flow.onset_us,flow.duo.zero,trace_total);}
        else probe_trace_brew(flow.onset_us,flow.zero_cg);
    }
    else if(flow.stage==CW_BREW&&trace_stage==CW_TDS&&flow.dual)probe_trace_resume();
    else if(flow.stage!=CW_BREW)probe_trace_stop();
    trace_stage=flow.stage;
}
// Home from anywhere: leaves roast color, ends the brew session (a connected scale
// stays connected and is offered on Home) and keeps Wi-Fi and Dropbox running.
static void go_home() {
    if(ui.firmware_open) {firmware_close();return;}
    if(app.mode==CW_APP_COLOR)color_mode_leave();
    cw_app_home(&app);
    if(ui.wifi_setup)wifi_setup_close();
    cw_home(&flow);transport_drop_rs();rs_requested=false;rs_epoch=0;
    if(!flow.scale_ready)transport_find_scale();
    int battery=ui.battery_percent;cw_network_view_t network=ui.network;
    ui={};ui.battery_percent=battery;ui.network=network;queue_read=queue_write=0;render_needed=true;
    ESP_LOGI("flow","Home generation=%u",flow.generation);
}
static void enter_color(int64_t now) {color_mode_enter(now);render_needed=true;ESP_LOGI("app","roast color");}
// With the scale connected the brew flow already waits at the brewer carousel.
static void enter_brew() {render_needed=true;ESP_LOGI("app","brew stage=%u",flow.stage);}
// Either scale counts for Home: a new connection opens brewing, losing the last
// one outside a brew returns Home.
// A link with a new epoch is a scale's new connection; Home re-announces links that
// were already up, and those must not reopen brewing.
static unsigned scale_epoch_seen,aux_epoch_seen;
static void app_link(bool fresh=false) {
    bool idle=!flow.interrupted&&(flow.stage==CW_HOME||flow.stage==CW_BREWER);
    cw_app_mode_t was=app.mode;cw_app_scale(&app,flow.scale_ready||flow.aux_ready,fresh,ui.wifi_setup||ui.firmware_open,idle);
    if(was==CW_APP_HOME&&app.mode==CW_APP_BREW)enter_brew();
    else if(was==CW_APP_BREW&&app.mode==CW_APP_HOME)go_home();
}
// Display-only view of the two-scale brew and the connected scales.
static void dual_view() {
    ui.dual.scales=(flow.scale_ready?1:0)+(flow.aux_ready?1:0);
    ui.dual.active=flow.dual&&(flow.stage==CW_BREW||flow.stage==CW_TDS||flow.stage==CW_REPORT)&&!flow.interrupted;
    ui.dual.out_valid=flow.dual&&flow.stage==CW_BREW&&flow.duo.phase==CW_DUO_BREWING;
    ui.dual.out_gps=flow.duo.out_cgps/100.f;
    ui.dual.open_valid=ui.dual.active&&cw_brewer_immersion(flow.brewer)&&flow.duo.open_valid&&flow.duo.open_us>=flow.onset_us;
    ui.dual.open_ms=ui.dual.open_valid?(uint32_t)((flow.duo.open_us-flow.onset_us)/1000):0;
    snprintf(ui.home_scales,sizeof ui.home_scales,"%s",flow.scale_ready&&flow.aux_ready?"BOOKOO + LEFU":flow.scale_ready?"BOOKOO":flow.aux_ready?"LEFU":"");
}
static void animate(int dir) {
    if(ui.firmware_open||app.mode!=CW_APP_BREW||flow.stage!=CW_BREWER)return;
    if(ui.carousel_dir) {if(queue_write-queue_read<16)queued[queue_write++%16]=dir;return;}
    ui.carousel_dir=dir;ui.carousel_from=flow.brewer;ui.carousel_progress=0;animation_start=now_us();render_needed=true;
}
static void action(int which) {
    if(!ui_enabled(flow,which,&ui)||ui.carousel_dir)return;
    if(ui.firmware_open) {firmware_action(which);return;}
    if(ui.wifi_setup) {wifi_setup_close();ui.wifi_setup=false;ui.held=0;render_needed=true;return;}
    if(app.mode==CW_APP_HOME) {
        if(which==UI_HOME_WIFI) {ui.wifi_setup=true;ui.held=0;wifi_setup_open();render_needed=true;}
        else if(which==UI_HOME_FIRMWARE)firmware_open();
        else if(which==UI_HOME_BREW&&cw_app_choose(&app,CW_APP_BREW))enter_brew();
        else if(which==UI_HOME_COLOR&&cw_app_choose(&app,CW_APP_COLOR))enter_color(now_us());
        return;
    }
    if(which==1&&(flow.stage==CW_HOME||(!flow.scale_ready&&flow.stage==CW_BREWER))) {ui.wifi_setup=true;ui.held=0;wifi_setup_open();render_needed=true;return;}
    if(which==1&&flow.stage==CW_BREW&&flow.result_valid) {cw_revisit_stopped(&flow);return;}
    if(which==1&&flow.stage==CW_BREWER) {cw_choose(&flow,flow.brewer);if(flow.stage==CW_EMPTY)save_brewer(flow.brewer);return;}
    // There is no Start: preparation arms pour detection by itself.
    if(which==1&&flow.stage==CW_EMPTY) {
        uint8_t b[6];if(cw_reset_dispatch(&flow,b)) {transport_write(b,flow.generation);ESP_LOGI("flow","Reset dispatched; no action-completed inference");}
    }
    // No Stop action while brewing: lifting the dripper ends the brew.
}
static void back() {
    if(ui.firmware_open) {firmware_close();return;}
    if(ui.wifi_setup) {wifi_setup_close();ui.wifi_setup=false;ui.held=0;render_needed=true;return;}
    if(app.mode==CW_APP_HOME)return;
    // Back from the brewer carousel is Home.
    if(flow.stage==CW_BREWER||flow.stage==CW_HOME) {go_home();return;}
    cw_back(&flow);
    transport_drop_rs();rs_requested=false;rs_epoch=0;if(!flow.scale_ready)transport_find_scale();
    int battery=ui.battery_percent;cw_network_view_t network=ui.network;
    ui={};ui.battery_percent=battery;ui.network=network;queue_read=queue_write=0;render_needed=true;
}
struct Inputs {
    bool next=false,select=false,touch=false,next_raw=false,select_raw=false;
    bool next_consumed=false,select_consumed=false,chord=false,touch_cancel=false;
    int touch_action=0,start_x=0,start_y=0,last_x=0,last_y=0;
    int64_t next_start=0,select_start=0,chord_start=0,next_change=0,select_change=0;
    unsigned next_gen=0,select_gen=0,touch_gen=0;
};
static Inputs in;
static void input_tick(BoardInput raw,int64_t now) {
    unsigned old_gen=flow.generation;bool old_touch=in.touch;
    if(!raw.touch_valid) {in.touch_cancel=true;raw.touch=false;}
    bool prev_next=in.next,prev_select=in.select;
    if(raw.next!=in.next_raw) {in.next_raw=raw.next;in.next_change=now;}
    if(raw.select!=in.select_raw) {in.select_raw=raw.select;in.select_change=now;}
    if(now-in.next_change>=15000)in.next=in.next_raw;
    if(now-in.select_change>=15000)in.select=in.select_raw;
    if(in.next&&!prev_next) {in.next_start=now;in.next_gen=flow.generation;in.next_consumed=false;}
    if(in.select&&!prev_select) {in.select_start=now;in.select_gen=flow.generation;in.select_consumed=ui.carousel_dir!=0;}
    if(in.next&&in.select&&!in.chord) {in.chord=true;in.chord_start=now;in.next_consumed=in.select_consumed=true;}
    if(in.next&&in.next_gen!=flow.generation)in.next_consumed=true;
    if(in.select&&in.select_gen!=flow.generation)in.select_consumed=true;
    if(in.chord) {
        if(in.chord_start&&in.next&&in.select&&now-in.chord_start>=900000) {go_home();in.chord_start=0;}
        if(!in.next&&!in.select)in.chord=false;
    } else {
        if(in.next&&!in.next_consumed&&now-in.next_start>=650000) {in.next_consumed=true;in.select_consumed=true;back();}
        if(!in.next&&prev_next&&!in.next_consumed&&in.next_gen==flow.generation) {
            if(!ui.wifi_setup&&!ui.firmware_open&&app.mode==CW_APP_BREW&&flow.stage==CW_BREWER)animate(1);
            else turn_page(1,now);
        }
        if(!in.select&&prev_select&&!in.select_consumed&&in.select_gen==flow.generation)action(1);
    }
    if(raw.touch&&!in.touch) {
        in.start_x=in.last_x=raw.x;in.start_y=in.last_y=raw.y;in.touch_gen=flow.generation;
        in.touch_action=ui.carousel_dir?0:ui_hit(flow,raw.x,raw.y,&ui);in.touch_cancel=ui.carousel_dir&&raw.y>=354;
    }
    if(raw.touch) {
        in.last_x=raw.x;in.last_y=raw.y;
        if(in.touch_action&&ui_hit(flow,raw.x,raw.y,&ui)!=in.touch_action)in.touch_cancel=true;
        if(in.touch_gen!=flow.generation)in.touch_cancel=true;
    }
    if(!raw.touch&&in.touch&&in.touch_gen==flow.generation) {
        if(in.touch_action) {if(!in.touch_cancel)action(in.touch_action);}
        else if(!in.touch_cancel) {
            int dx=in.last_x-in.start_x,dy=in.last_y-in.start_y;
            if(!ui.wifi_setup&&!ui.firmware_open&&std::abs(dx)>50&&std::abs(dx)>std::abs(dy)) {
                if(app.mode==CW_APP_BREW&&flow.stage==CW_BREWER)animate(dx<0?1:-1);
                else turn_page(dx<0?1:-1,now);
            }
        }
    }
    in.touch=raw.touch;
    int old=ui.held;ui.held=0;
    if(!in.chord) {
        if(in.select&&!in.select_consumed&&in.select_gen==flow.generation) {int a=1;if(ui_enabled(flow,a,&ui))ui.held=a;}
        if(in.touch&&!in.touch_cancel&&in.touch_gen==flow.generation)ui.held=in.touch_action;
    }
    if(old!=ui.held||prev_next!=in.next||prev_select!=in.select||raw.touch!=old_touch||old_gen!=flow.generation)render_needed=true;
}
// Second-scale notification statistics for the serial `scales` command.
static unsigned aux_count;static int64_t aux_first_us,aux_last_us,aux_max_gap_us;
// Scale and probe-clock transitions must publish the same draft/trace and start
// RS discovery. In particular, a held CK811 reading can end on a probe tick.
static void backup_record(const cw_brew_record_t &record) {
    struct timeval wall;gettimeofday(&wall,nullptr);
    int64_t utc=wall.tv_sec>1735689600?((int64_t)wall.tv_sec*1000000+wall.tv_usec-now_us()+record.captured_us)/1000000:0;
    if(!brew_backup_save(&record,utc))ESP_LOGW("record","local summary backup failed; id=%s revision=%u",record.id,record.revision);
}
static void flow_follow(cw_stage_t before,int64_t us,bool record_event) {
    trace_follow(us);
    if(record_event) {
        cw_brew_record_t record;
        if(cw_record_event(&records,before,&flow,us,&record)) {
            cw_record_status_t pending={};pending.state=CW_RECORD_SAVING;pending.revision=record.revision;snprintf(pending.id,sizeof pending.id,"%s",record.id);ui.record=pending;ui.record_visible=true;
            backup_record(record);
            dropbox_setup_record(&record,!record.complete?probe_trace_snapshot():nullptr);
        }
    }
    if(flow.stage==CW_TDS&&!rs_requested) {rs_requested=true;transport_find_rs();}
    if(before!=flow.stage) {
        ui.held=0;ui.result_page=0;ui.page_us=us;
        ESP_LOGI("flow","stage=%u generation=%u brew=%u dose_valid=%u empty=%ld beverage=%ld",flow.stage,flow.generation,flow.scale.brew_id,flow.scale.dose_valid,(long)flow.empty_cg,(long)flow.beverage_cg);
        if(flow.stage==CW_REPORT)ESP_LOGI("flow","REPORT dose=%.1f water=%.1f beverage=%.1f time_ms=%u tds=%.8f extraction=%.8f",flow.result.dose_cg/100.,flow.result.water_cg/100.,flow.beverage_cg/100.,(unsigned)flow.result.elapsed_ms,(double)flow.tds,(double)flow.extraction);
    }
}
static void receive(const demo_event_t &e) {
    cw_stage_t before=flow.stage;
    switch(e.kind) {
    // Losing a scale before the pour restarts at the carousel (the flow does that);
    // mid-brew it interrupts.
    case DE_SCALE_UP: {bool fresh=e.epoch!=scale_epoch_seen;scale_epoch_seen=e.epoch;cw_scale_link(&flow,true,e.epoch);app_link(fresh);break;}
    case DE_SCALE_DOWN:cw_scale_link(&flow,false,e.epoch);probe_trace_weight(false,0);app_link();break;
    case DE_AUX_UP: {
        bool fresh=e.epoch!=aux_epoch_seen;aux_epoch_seen=e.epoch;cw_aux_link(&flow,true,e.epoch);app_link(fresh);
        ESP_LOGI("flow","second scale up fresh=%u dual=%u stage=%u",fresh,flow.dual,flow.stage);break;
    }
    case DE_AUX_DOWN:cw_aux_link(&flow,false,e.epoch);probe_trace_aux(false,0);app_link();break;
    case DE_AUX:
        if(aux_count&&e.us>aux_last_us) {int64_t gap=e.us-aux_last_us;if(gap>aux_max_gap_us)aux_max_gap_us=gap;}
        if(!aux_count)aux_first_us=e.us;
        aux_count++;aux_last_us=e.us;
        cw_aux_weight(&flow,e.epoch,e.data.aux_cg,e.us);
        if(flow.aux_ready&&e.epoch==flow.aux_epoch)probe_trace_aux(true,flow.aux_cg);
        break;
    case DE_RS_UP:rs_epoch=e.epoch;cw_rs_link(&flow,true);break;
    case DE_RS_DOWN:cw_rs_link(&flow,false);if(flow.stage==CW_TDS)transport_find_rs();break;
    case DE_SCALE:
        cw_scale_message(&flow,e.epoch,&e.data.scale,e.us);
        if(e.data.scale.kind==BOOKOO_WEIGHT&&flow.scale_ready&&e.epoch==flow.scale.epoch)probe_trace_weight(true,cw_mass_cg(e.data.scale.weight_cg));
        break;
    case DE_RS:if(e.epoch==rs_epoch)cw_rs_message(&flow,&e.data.rs,e.us);break;
    case DE_WRITE:if(e.generation==flow.generation)cw_write_done(&flow,e.success);break;
    case DE_WIFI:ui.network=e.data.network;network_activity_us=e.us;ESP_LOGI("network","state=%u setup=%u paired=%u saved=%u reason=%d",ui.network.state,ui.network.setup_active,ui.network.paired,ui.network.saved,ui.network.reason);break;
    case DE_RECORD:
        color_mode_record(e.data.record);
        if(!strcmp(e.data.record.id,records.record.id)&&(!ui.record_visible||e.data.record.revision>=ui.record.revision)) {ui.record=e.data.record;ui.record_visible=true;network_activity_us=e.us;}
        break;
    case DE_ERROR:snprintf(flow.error,sizeof flow.error,"%s",e.data.error);ESP_LOGE("flow","%s",flow.error);break;
    }
    flow_follow(before,e.us,e.kind==DE_SCALE||e.kind==DE_RS||e.kind==DE_AUX);
    render_needed=true;
}
struct ConsoleCommand {char line[80];};
static void console_task(void *arg) {
    char line[80];unsigned n=0;
    for(;;) {
        int ch=getchar();if(ch==EOF) {clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}
        if(ch=='\n'||ch=='\r') {if(n) {line[n]=0;ConsoleCommand c;snprintf(c.line,sizeof c.line,"%s",line);configASSERT(xQueueSend(console_commands,&c,0)==pdTRUE);n=0;}}
        else if(n<sizeof line-1)line[n++]=ch;
    }
}
extern "C" void app_main() {
    // Enroll before NVS/board/network initialization as well as steady-state:
    // a hung startup must reset a pending trial so the bootloader can roll back.
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    setvbuf(stdin,nullptr,_IONBF,0);ESP_ERROR_CHECK(nvs_flash_init());
    events=xQueueCreate(128,sizeof(demo_event_t));console_commands=xQueueCreate(4,sizeof(ConsoleCommand));configASSERT(events&&console_commands);
    cw_init(&flow);flow.brewer=saved_brewer();board_init();color_sensor_init();cw_app_init(&app);
    probe_trace_init();ui.home=true;ui.battery_percent=board_battery_percent();ui_tick(flow,ui,now_us());ui_draw(flow,ui);board_present();
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char session[40];
    snprintf(session,sizeof session,"%02x%02x%02x%02x%02x%02x-%08lx%08lx",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(unsigned long)esp_random(),(unsigned long)esp_random());cw_record_init(&records,session);color_mode_init(session);
    wifi_setup_init();ESP_ERROR_CHECK(firmware_update_init());transport_init();xTaskCreate(console_task,"console",4096,nullptr,2,nullptr);
    const int64_t health_started=now_us();bool boot_health_checked=false;
    ESP_LOGI("demo","coffee-watch-demo-v3; battery=%d; UI=%dx%d",ui.battery_percent,(int)board_canvas().width(),(int)board_canvas().height());
    int64_t last_draw=0,last_activity=now_us(),last_battery=last_activity;
    int battery_percent=ui.battery_percent;
    constexpr int64_t idle_limit_us=10LL*60*1000000;
    unsigned last_generation=flow.generation;
    auto last_phase=flow.scale.phase;
    bool last_scale=flow.scale_ready,last_rs=flow.rs_ready,last_interrupted=flow.interrupted;
    unsigned probe_seq=0;
    int64_t last_sensor_poll=-1000000;cw_app_mode_t last_mode=app.mode;
    for(;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        demo_event_t e;unsigned n=0;while(n++<32&&xQueueReceive(events,&e,0)==pdTRUE)receive(e);
        // Each new 10 Hz bed-probe reading feeds pour detection once.
        probe_reading_t probe;probe_trace_reading(&probe);
        if(probe.seq!=probe_seq) {
            probe_seq=probe.seq;cw_stage_t before=flow.stage;bool was_interrupted=flow.interrupted;
            cw_probe_message(&flow,probe.valid,probe.celsius,probe.us);
            if(flow.stage!=before) {
                flow_follow(before,probe.us,true);
                ESP_LOGI("flow","stage=%u generation=%u zero=%ld onset=%lld",flow.stage,flow.generation,(long)flow.zero_cg,(long long)flow.onset_us);
            }
            if(flow.stage!=before||flow.interrupted!=was_interrupted)render_needed=true;
        }
        int64_t now=now_us();
        BoardInput raw=board_input();
        if(raw.next||raw.select||(raw.touch_valid&&raw.touch))last_activity=now;
        // Home polls G5/G6 twice a second; a newly attached color sensor picks roast color.
        if(app.mode==CW_APP_HOME&&now-last_sensor_poll>=500000) {
            last_sensor_poll=now;bool present=app.sensor_present;
            cw_app_sensor(&app,color_sensor_probe(),ui.wifi_setup||ui.firmware_open);
            if(app.sensor_present!=present)render_needed=true;
            if(app.mode==CW_APP_COLOR)enter_color(now);
        }
        ui.home=app.mode==CW_APP_HOME;ui.home_scale=flow.scale_ready||flow.aux_ready;ui.home_color=app.sensor_present;dual_view();
        if(app.mode==CW_APP_COLOR) {
            if(color_mode_tick(now))last_activity=now;
            if(color_mode_input(raw,now,ui))go_home();
            else if(color_mode_sensor_lost(now)) {
                ESP_LOGW("app","color sensor stopped answering; Home");
                color_mode_leave();cw_app_sensor_lost(&app);go_home();
            }
        } else input_tick(raw,now);
        // A brew session that ends with no scale at all (Back from Waiting TDS with the
        // scale off) is Home; with the second scale alone the brew waits for BOOKOO.
        if(app.mode==CW_APP_BREW&&flow.stage==CW_HOME&&!flow.interrupted&&!flow.aux_ready)go_home();
        if(app.mode!=last_mode) {last_mode=app.mode;last_activity=now;render_needed=true;}
        ui.home=app.mode==CW_APP_HOME;ui.home_scale=flow.scale_ready||flow.aux_ready;ui.home_color=app.sensor_present;dual_view();
        trace_follow(now);
        if(flow.duo.slow_drip_seq!=slow_drip_seen) {
            slow_drip_seen=flow.duo.slow_drip_seq;
            if(flow.dual&&flow.stage==CW_BREW&&!flow.interrupted&&slow_drip_seen) {
                audio_chirp();ESP_LOGI("flow","slow-drip hint seq=%u out_cg=%ld",slow_drip_seen,(long)flow.duo.out_cg);
            }
        }
        if(now-last_battery>=1000000) {
            last_battery=now;battery_percent=board_battery_percent();
        }
        if(ui.battery_percent!=battery_percent) {
            ESP_LOGI("battery","level=%d",battery_percent);
            ui.battery_percent=battery_percent;render_needed=true;
        }
        firmware_sync(now);
        if(!boot_health_checked&&now-health_started>=10000000) {
            boot_health_checked=true;
            // NVS startup was checked above without erase; a rendered first frame,
            // ten seconds of event-loop heartbeats and a live main watchdog are
            // local health. Network, scale and optional color sensor are not gates.
            esp_err_t health=firmware_update_mark_running_valid_after_health(board_canvas().width()==468&&board_canvas().height()==466,true);
            ESP_LOGI("update","boot health result=%s",esp_err_to_name(health));
        }
        if(ui.carousel_dir) {
            float t=(now-animation_start)/(float)design::motion::carousel_us;
            if(t>=1) {
                flow.brewer=(ui.carousel_from+ui.carousel_dir+4)%4;ui.carousel_dir=0;
                if(queue_read<queue_write)animate(queued[queue_read++%16]);
            } else ui.carousel_progress=1-std::pow(1-t,3);
            render_needed=true;
        }
        ConsoleCommand cmd;
        if(xQueueReceive(console_commands,&cmd,0)==pdTRUE) {
            if(app.mode==CW_APP_COLOR&&color_mode_console(cmd.line,now))last_activity=now;
            else if(!strcmp(cmd.line,"screen"))board_dump();
            else if(!strcmp(cmd.line,"scales")) {
                double span=(aux_last_us-aux_first_us)/1e6;
                printf("SCALES bookoo=%u aux=%u aux_cg=%ld aux_count=%u aux_hz=%.2f aux_max_gap_ms=%lld aux_last_ms_ago=%lld\n",flow.scale_ready,flow.aux_ready,(long)flow.aux_cg,aux_count,
                    aux_count>1&&span>0?(aux_count-1)/span:0.0,(long long)(aux_max_gap_us/1000),(long long)(aux_count?(now-aux_last_us)/1000:-1));
            }
            else if(!strcmp(cmd.line,"beep")) {audio_chirp();printf("BEEP quiet 30ms requested\n");}
            else if(!strcmp(cmd.line,"update status")) {
                firmware_update_status_t s={};firmware_update_get_status(&s);
                printf("UPDATE state=%u busy=%u bytes=%u total=%u version=%s error=%d message=%s\n",(unsigned)s.state,s.busy,(unsigned)s.received_bytes,(unsigned)s.total_bytes,s.version,(int)s.error,s.message);
            }
#if !CONFIG_CW_LAUNCHER_APP
            else if(!strcmp(cmd.line,"update open")) {last_activity=now;firmware_open();}
            else if(!strcmp(cmd.line,"update cancel")) {last_activity=now;if(ui.firmware_open)firmware_close();}
#endif
            else if(!strcmp(cmd.line,"dropbox status")) {
                char json[384];if(dropbox_setup_status(json,sizeof json)>0)printf("DROPBOX %s\n",json);
            }
            else if(!strcmp(cmd.line,"dropbox check")||!strcmp(cmd.line,"dropbox test")) {
                cw_dropbox_command_t check={};check.op=!strcmp(cmd.line,"dropbox test")?4:3;
                printf("%s\n",dropbox_setup_submit(&check)?"DROPBOX check queued":"DROPBOX busy");
            }
            else if(!strcmp(cmd.line,"record status")) {
                printf("RECORD state=%u revision=%u id=%s path=%s error=%s\n",ui.record.state,ui.record.revision,ui.record.id,ui.record.path,ui.record.error);
            }
            else if(!strcmp(cmd.line,"record backups")) {
                char json[2048];unsigned count=0;
                for(unsigned i=0;i<8;i++)if(brew_backup_json(i,json,sizeof json)>0) {printf("BACKUP %s\n",json);count++;}
                printf("BACKUPS count=%u trace_samples_persisted=false\n",count);
            }
            else if(!strcmp(cmd.line,"mode"))printf("MODE %s sensor=%u scale=%u\n",app.mode==CW_APP_HOME?"home":app.mode==CW_APP_BREW?"brew":"color",app.sensor_present,flow.scale_ready);
            else if(!strcmp(cmd.line,"netstatus"))printf("NETWORK state=%u setup=%u paired=%u saved=%u reason=%d setup_error=%d\n",ui.network.state,ui.network.setup_active,ui.network.paired,ui.network.saved,ui.network.reason,ui.network.setup_error);
            else if(!strcmp(cmd.line,"status")) {
                printf("STATUS stage=%u generation=%u scale=%u rs=%u phase=%u dose_valid=%u dose_cg=%ld weight_cg=%ld empty_cg=%ld beverage_cg=%ld\n",flow.stage,flow.generation,flow.scale_ready,flow.rs_ready,flow.scale.phase,flow.scale.dose_valid,(long)flow.scale.dose_cg,(long)flow.live_cg,(long)flow.empty_cg,(long)flow.beverage_cg);
                printf("DUAL aux=%u aux_cg=%ld dual=%u phase=%u total=%u water_cg=%ld out_cg=%ld out_cgps=%ld peak_cgps=%ld open=%u\n",flow.aux_ready,(long)flow.aux_cg,flow.dual,flow.duo.phase,flow.duo.total,
                    (long)flow.duo.water_cg,(long)flow.duo.out_cg,(long)flow.duo.out_cgps,(long)flow.duo.peak_cgps,flow.duo.open_valid);
            }
            else if(!strcmp(cmd.line,"probe")) {
                probe_reading_t p;probe_trace_reading(&p);
                printf("PROBE valid=%u celsius=%.2f ratio=%.4f ntc_mv=%d ref_mv=%d trace_active=%u brew_started=%u samples=%u truncated=%u\n",p.valid,(double)p.celsius,(double)p.ratio,p.ntc_mv,p.ref_mv,p.active,p.brew_started,p.count,p.truncated);
            }
            else if(!strcmp(cmd.line,"home")) {last_activity=now;go_home();}
            // The Home tiles from the console; only a connected device can be opened.
            else if(!strcmp(cmd.line,"open brew")||!strcmp(cmd.line,"open color")) {last_activity=now;if(app.mode==CW_APP_HOME&&!ui.wifi_setup&&!ui.firmware_open)action(cmd.line[5]=='b'?UI_HOME_BREW:UI_HOME_COLOR);}
            else if(!strcmp(cmd.line,"next")) {last_activity=now;animate(1);}
            else if(!strcmp(cmd.line,"select")) {last_activity=now;action(1);}
            else if(!strcmp(cmd.line,"back")) {last_activity=now;back();}
            render_needed=true;
        }
        if(flow.generation!=last_generation||flow.scale.phase!=last_phase||
           flow.scale_ready!=last_scale||flow.rs_ready!=last_rs||flow.interrupted!=last_interrupted) {
            last_activity=now;
            last_generation=flow.generation;last_phase=flow.scale.phase;
            last_scale=flow.scale_ready;last_rs=flow.rs_ready;last_interrupted=flow.interrupted;
        }
        uint8_t initial_packet[6];
        if(cw_initial_request(&flow,initial_packet)) {ESP_LOGI("flow","Scale initialization command=%02X param=%02X",initial_packet[2],initial_packet[3]);transport_write(initial_packet,flow.generation);}
        if(network_activity_us>last_activity)last_activity=network_activity_us;
        // Global product idle policy. Weight updates and repainting are not activity.
        if(now-last_activity>=idle_limit_us&&!(ui.firmware_open&&ui.firmware_update.busy)) {
            ESP_LOGI("demo","No interaction or state change for 10 minutes; shutting down");
            board_shutdown();
            vTaskSuspend(nullptr);
        }
        if(app.mode==CW_APP_COLOR) {
            // The meter redraws at 10 Hz (live index, warm-up clock), faster while animating.
            if(color_mode_animating(now)||now-last_draw>=100000)render_needed=true;
            if(render_needed&&now-last_draw>=design::motion::frame_us) {color_mode_draw(ui);board_present();last_draw=now;render_needed=false;}
        } else {
            if(app.mode==CW_APP_BREW&&ui_result_pages(flow)>1&&!ui.wifi_setup&&now-ui.page_us>=result_page_us)turn_page(1,now);
            if(ui_tick(flow,ui,now))render_needed=true;
            if(render_needed&&now-last_draw>=design::motion::frame_us) {ui_draw(flow,ui);board_present();last_draw=now;render_needed=false;}
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
