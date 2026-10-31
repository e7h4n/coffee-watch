// SPDX-License-Identifier: MIT
#include "ui.h"
#include "design.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstring>
#include <cmath>
static QueueHandle_t events,console_commands;
static cw_flow_t flow;
static UiState ui;
static unsigned rs_epoch;
static bool render_needed=true,rs_requested;
static int64_t animation_start;
static int queued[16];static unsigned queue_read,queue_write;
extern "C" void demo_post(const demo_event_t *event) {configASSERT(xQueueSend(events,event,0)==pdTRUE);}
static int64_t now_us() {return esp_timer_get_time();}
static void go_home() {
    if(flow.stage==CW_READY) {uint8_t b[6];if(cw_exit_ready_request(&flow,b))transport_write(b,flow.generation);}
    cw_home(&flow);transport_drop_rs();rs_requested=false;rs_epoch=0;
    if(!flow.scale_ready)transport_find_scale();
    ui={};queue_read=queue_write=0;render_needed=true;
    ESP_LOGI("flow","Home generation=%u",flow.generation);
}
static void animate(int dir) {
    if(flow.stage!=CW_BREWER)return;
    if(ui.carousel_dir) {if(queue_write-queue_read<16)queued[queue_write++%16]=dir;return;}
    ui.carousel_dir=dir;ui.carousel_from=flow.brewer;ui.carousel_progress=0;animation_start=now_us();render_needed=true;
}
static void action(int which) {
    if(!ui_enabled(flow,which)||ui.carousel_dir)return;
    if(which==1&&flow.stage==CW_BREW&&flow.scale.phase==BOOKOO_STOPPED) {cw_revisit_stopped(&flow);return;}
    if(which==1&&flow.stage==CW_BREWER) {cw_choose(&flow,flow.brewer);return;}
    if(which==1&&flow.stage==CW_PREP) {
        uint8_t b[6];if(cw_ready_request(&flow,b))transport_write(b,flow.generation);
        return;
    }
    if(which==1&&flow.stage==CW_EMPTY) {
        uint8_t b[6];if(cw_reset_dispatch(&flow,b)) {transport_write(b,flow.generation);ESP_LOGI("flow","Reset dispatched; no action-completed inference");}
    } else if(which==1&&flow.stage==CW_BREW) {
        bookoo_command_t cmd=BOOKOO_CMD_STOP;uint8_t b[6];
        if(cw_timer_request(&flow,cmd,b))transport_write(b,flow.generation);
    }
}
static void back() {
    cw_back(&flow);
    if(flow.back_pending&&!flow.scale.pending&&!flow.write_pending) {
        uint8_t b[6];bool dispatch=flow.stage==CW_READY?cw_exit_ready_request(&flow,b):cw_timer_request(&flow,BOOKOO_CMD_STOP,b);
        if(dispatch)transport_write(b,flow.generation);
    }
    transport_drop_rs();rs_requested=false;rs_epoch=0;if(!flow.scale_ready)transport_find_scale();
    ui={};queue_read=queue_write=0;render_needed=true;
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
            if(flow.stage==CW_BREWER)animate(1);
        }
        if(!in.select&&prev_select&&!in.select_consumed&&in.select_gen==flow.generation)action(1);
    }
    if(raw.touch&&!in.touch) {
        in.start_x=in.last_x=raw.x;in.start_y=in.last_y=raw.y;in.touch_gen=flow.generation;
        in.touch_action=ui.carousel_dir?0:ui_hit(flow,raw.x,raw.y);in.touch_cancel=ui.carousel_dir&&raw.y>=354;
    }
    if(raw.touch) {
        in.last_x=raw.x;in.last_y=raw.y;
        if(in.touch_action&&ui_hit(flow,raw.x,raw.y)!=in.touch_action)in.touch_cancel=true;
        if(in.touch_gen!=flow.generation)in.touch_cancel=true;
    }
    if(!raw.touch&&in.touch&&in.touch_gen==flow.generation) {
        if(in.touch_action) {if(!in.touch_cancel)action(in.touch_action);}
        else if(!in.touch_cancel) {
            int dx=in.last_x-in.start_x,dy=in.last_y-in.start_y;
            if(std::abs(dx)>50&&std::abs(dx)>std::abs(dy)) {
                if(flow.stage==CW_BREWER)animate(dx<0?1:-1);
                else if(flow.stage==CW_REPORT||flow.stage==CW_CHART) {flow.stage=flow.stage==CW_REPORT?CW_CHART:CW_REPORT;flow.generation++;}
            }
        }
    }
    in.touch=raw.touch;
    int old=ui.held;ui.held=0;
    if(!in.chord) {
        if(in.select&&!in.select_consumed&&in.select_gen==flow.generation) {int a=1;if(ui_enabled(flow,a))ui.held=a;}
        if(in.touch&&!in.touch_cancel&&in.touch_gen==flow.generation)ui.held=in.touch_action;
    }
    if(old!=ui.held||prev_next!=in.next||prev_select!=in.select||raw.touch!=old_touch||old_gen!=flow.generation)render_needed=true;
}
static void receive(const demo_event_t &e) {
    cw_stage_t before=flow.stage;
    switch(e.kind) {
    case DE_SCALE_UP:cw_scale_link(&flow,true,e.epoch);break;
    case DE_SCALE_DOWN:cw_scale_link(&flow,false,e.epoch);break;
    case DE_RS_UP:rs_epoch=e.epoch;cw_rs_link(&flow,true);break;
    case DE_RS_DOWN:cw_rs_link(&flow,false);if(flow.stage==CW_TDS)transport_find_rs();break;
    case DE_SCALE:cw_scale_message(&flow,e.epoch,&e.data.scale,e.us);break;
    case DE_RS:if(e.epoch==rs_epoch)cw_rs_message(&flow,&e.data.rs,e.us);break;
    case DE_WRITE:if(e.generation==flow.generation)cw_write_done(&flow,e.success);break;
    case DE_ERROR:snprintf(flow.error,sizeof flow.error,"%s",e.data.error);ESP_LOGE("flow","%s",flow.error);break;
    }
    if(flow.stage==CW_TDS&&!rs_requested) {rs_requested=true;transport_find_rs();}
    if(before!=flow.stage) {
        ui.held=0;
        ESP_LOGI("flow","stage=%u generation=%u brew=%u dose_valid=%u empty=%ld beverage=%ld",flow.stage,flow.generation,flow.scale.brew_id,flow.scale.dose_valid,(long)flow.empty_cg,(long)flow.beverage_cg);
        if(flow.stage==CW_REPORT)ESP_LOGI("flow","REPORT dose=%.1f water=%.1f beverage=%.1f time_ms=%u tds=%.8f extraction=%.8f",flow.scale.result.dose_cg/100.,flow.scale.result.water_cg/100.,flow.beverage_cg/100.,(unsigned)flow.scale.result.elapsed_ms,(double)flow.tds,(double)flow.extraction);
    }
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
    setvbuf(stdin,nullptr,_IONBF,0);ESP_ERROR_CHECK(nvs_flash_init());
    events=xQueueCreate(128,sizeof(demo_event_t));console_commands=xQueueCreate(4,sizeof(ConsoleCommand));configASSERT(events&&console_commands);
    cw_init(&flow);board_init();ui_tick(flow,now_us());ui_draw(flow,ui);board_present();
    transport_init();xTaskCreate(console_task,"console",4096,nullptr,2,nullptr);
    ESP_LOGI("demo","coffee-watch-demo-v3; UI=%dx%d",(int)board_canvas().width(),(int)board_canvas().height());
    int64_t last_draw=0,last_activity=now_us();
    constexpr int64_t idle_limit_us=10LL*60*1000000;
    unsigned last_generation=flow.generation;
    auto last_phase=flow.scale.phase;
    bool last_scale=flow.scale_ready,last_rs=flow.rs_ready,last_interrupted=flow.interrupted;
    for(;;) {
        demo_event_t e;unsigned n=0;while(n++<32&&xQueueReceive(events,&e,0)==pdTRUE)receive(e);
        int64_t now=now_us();
        BoardInput raw=board_input();
        if(raw.next||raw.select||(raw.touch_valid&&raw.touch))last_activity=now;
        input_tick(raw,now);
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
            if(!strcmp(cmd.line,"screen"))board_dump();
            else if(!strcmp(cmd.line,"status"))printf("STATUS stage=%u generation=%u scale=%u rs=%u phase=%u dose_valid=%u dose_cg=%ld weight_cg=%ld empty_cg=%ld beverage_cg=%ld\n",flow.stage,flow.generation,flow.scale_ready,flow.rs_ready,flow.scale.phase,flow.scale.dose_valid,(long)flow.scale.dose_cg,(long)flow.live_cg,(long)flow.empty_cg,(long)flow.beverage_cg);
            else if(!strcmp(cmd.line,"home")) {last_activity=now;go_home();}
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
        // Global product idle policy. Weight updates and repainting are not activity.
        if(now-last_activity>=idle_limit_us) {
            ESP_LOGI("demo","No interaction or state change for 10 minutes; shutting down");
            board_shutdown();
            vTaskSuspend(nullptr);
        }
        if(ui_tick(flow,now))render_needed=true;
        if(render_needed&&now-last_draw>=design::motion::frame_us) {ui_draw(flow,ui);board_present();last_draw=now;render_needed=false;}
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
