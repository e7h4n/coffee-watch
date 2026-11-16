// SPDX-License-Identifier: MIT
#include "ui.h"
#include "icons.h"
#include "design.h"
#include "smooth_fonts.h"
#include "probe_trace.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
static constexpr uint32_t BG=design::background,FG=design::foreground,MUTED=design::muted,ACCENT=design::accent,LINE=design::line,BUTTON=design::button;
static uint32_t mix(uint32_t a,uint32_t b,unsigned alpha);
static float enter=1.f,value_flash=0.f,breath=0.f;
static int64_t entered_us=0,changed_us=0;
static int previous_page=-1;
static int32_t previous_value=0;
static bool have_value=false;
static const char *names[]={"V60","Switch","AeroPress","Hoop"};
static LGFX_Sprite *c;
static const UiState *cu;
static void text(const char *s,int x,int y,int px,uint32_t color=FG,int max_width=0,bool ink_center=false,lgfx::textdatum_t datum=middle_center) {
    struct FontSlot {lgfx::PointerWrapper data;lgfx::VLWfont font;bool loaded=false;};
    static FontSlot slots[sizeof(font_sizes)/sizeof(font_sizes[0])];
    int index=0;
    for(unsigned i=0;i<sizeof(font_sizes)/sizeof(font_sizes[0]);i++)if(font_sizes[i]<=px)index=i;
    if(px>=design::type::hero&&!max_width)max_width=260;
    for(;;) {
        if(font_sizes[index]==60&&std::strcmp(s,"Ready"))--index;
        auto &slot=slots[index];
        if(!slot.loaded) {slot.data.set(font_data[index]);configASSERT(slot.font.loadFont(&slot.data));slot.loaded=true;}
        c->setFont(&slot.font);c->setTextSize(1);c->setTextDatum(datum);
        if(!max_width||c->textWidth(s)<=max_width||index==0)break;
        --index;
    }
    if(ink_center) {
        // Center the visible glyphs, not the font's ascent/descent padding.
        const uint8_t *data=font_data[index];
        auto be32=[](const uint8_t *p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);};
        int top=1000,bottom=-1000;
        for(const unsigned char *ch=(const unsigned char*)s;*ch;ch++) {
            if(*ch==' ')continue;
            for(int i=0;i<be32(data);i++) {
                const uint8_t *g=data+24+i*28;
                if(be32(g)!=*ch)continue;
                int height=be32(g+4),ascent=be32(g+16);
                top=std::min(top,-ascent);bottom=std::max(bottom,height-ascent);break;
            }
        }
        if(top<=bottom) {c->setTextDatum(baseline_center);y-=(int)std::lround((top+bottom-1)*.5f);}
    }
    c->setTextColor(mix(BG,color,(unsigned)(enter*255)));
    c->drawString(s,x,y+(y<design::button_top?(int)((1-enter)*12):0));
}
static void fmt(int x,int y,int px,uint32_t color,const char *format,...) {
    char b[80];va_list a;va_start(a,format);vsnprintf(b,sizeof b,format,a);va_end(a);text(b,x,y,px,color);
}
static void time_text(uint32_t ms,int x,int y,int size,uint32_t color=FG) {fmt(x,y,size,color,"%u:%02u",(unsigned)(ms/60000),(unsigned)(ms/1000%60));}
static void field(const char *label,const char *value,int x,int y,int size=32,int width=160) {text(label,x,y,design::type::label,MUTED,width);text(value,x,y+34,size,FG,width);}
static void bottom(const char *label,bool enabled,bool held) {
    c->fillRect(0,design::button_top,c->width(),design::button_height,held&&enabled?ACCENT:BUTTON);c->drawFastHLine(0,design::button_top,c->width(),LINE);
    text(label,design::center_x,design::button_center_y,design::type::button,held&&enabled?BG:(enabled?ACCENT:MUTED),0,true);
}
static uint32_t mix(uint32_t a,uint32_t b,unsigned alpha) {
    unsigned r=(((a>>16)&255)*(255-alpha)+((b>>16)&255)*alpha)/255;
    unsigned g=(((a>>8)&255)*(255-alpha)+((b>>8)&255)*alpha)/255;
    unsigned blue=((a&255)*(255-alpha)+(b&255)*alpha)/255;return r<<16|g<<8|blue;
}
static void icon(unsigned id,float x,float y,float size,uint32_t color) {
    int left=(int)(x-size/2),top=(int)(y-size/2),n=(int)size;
    if(n<=0)return;
    for(int dy=0;dy<n;dy++) {
        int yy=top+dy;if(yy<0||yy>=354)continue;
        unsigned sy=(unsigned)(dy*256/n);
        for(int dx=0;dx<n;dx++) {
            int xx=left+dx;if(xx<0||xx>=c->width())continue;
            unsigned a=brewer_icons[id%4][sy*256+dx*256/n];
            if(a)c->drawPixel(xx,yy,mix(BG,color,a));
        }
    }
}
static bool has_curve() {probe_reading_t r;probe_trace_reading(&r);return r.brew_started&&!r.active&&r.count>0;}
int ui_result_pages(const cw_flow_t &s) {
    if(s.interrupted)return 0;
    if(s.stage==CW_TDS)return has_curve()?2:1;
    if(s.stage==CW_REPORT)return has_curve()?3:2;
    return 0;
}
bool ui_curve_page(const cw_flow_t &s,const UiState &u) {return (s.stage==CW_TDS&&u.result_page==1)||(s.stage==CW_REPORT&&u.result_page==2);}
bool ui_tick(const cw_flow_t &s,const UiState &u,int64_t now) {
    int page=u.home?102:s.interrupted?100:(!s.scale_ready&&s.stage!=CW_TDS&&s.stage!=CW_REPORT?101:(int)s.stage*4+u.result_page);
    if(page!=previous_page) {previous_page=page;entered_us=now;have_value=false;}
    enter=std::clamp((now-entered_us)/(float)design::motion::enter_us,0.f,1.f);
    enter=1-std::pow(1-enter,3);
    int32_t value=s.stage==CW_BEVERAGE?s.live_cg-s.empty_cg:s.live_cg;
    if(!have_value||value!=previous_value) {previous_value=value;changed_us=now;have_value=true;}
    bool numeric=s.stage==CW_EMPTY||s.stage==CW_PREP||s.stage==CW_BEVERAGE;
    value_flash=numeric?1-std::clamp((now-changed_us)/(float)design::motion::value_us,0.f,1.f):0;
    breath=.5f-.5f*std::cos((now%design::motion::breath_us)*6.2831853f/design::motion::breath_us);
    // Ready breathes and shows the live bed probe.
    bool waiting=!s.interrupted&&s.stage==CW_READY;
    // The brew chart follows the 10 Hz probe trace, independent of scale notifications.
    static int64_t last_chart_us=0;
    bool chart=!s.interrupted&&s.stage==CW_BREW&&now-last_chart_us>=100000;
    if(chart)last_chart_us=now;
    return enter<1||value_flash>0||waiting||chart;
}
static uint32_t number_color() {return mix(FG,ACCENT,(unsigned)(value_flash*90));}
// Analytic one-pixel coverage keeps capsule ends smooth at native resolution.
static float capsule_coverage(float px,float py,float width,float height) {
    if(width<=0)return 0;
    float radius=std::min(width,height)*.5f;
    float dx=std::abs(px-width*.5f)-(width*.5f-radius);
    float dy=std::abs(py-height*.5f)-(height*.5f-radius);
    float ax=std::max(dx,0.f),ay=std::max(dy,0.f);
    float distance=std::sqrt(ax*ax+ay*ay)+std::min(std::max(dx,dy),0.f)-radius;
    return std::clamp(.5f-distance,0.f,1.f);
}
static void stability_bar(float progress) {
    constexpr int x=126,y=306,width=216,height=12;
    constexpr uint32_t track=0x4b5350;
    float filled=width*std::clamp(progress,0.f,1.f);
    for(int py=-1;py<=height;py++)for(int px=-1;px<=width;px++) {
        float a=capsule_coverage(px+.5f,py+.5f,width,height);
        float b=capsule_coverage(px+.5f,py+.5f,filled,height);
        if(a<=0&&b<=0)continue;
        uint32_t color=mix(BG,track,(unsigned)(a*255));
        color=mix(color,ACCENT,(unsigned)(b*255));
        c->drawPixel(x+px,y+py,color);
    }
}
bool ui_enabled(const cw_flow_t &s,int action,const UiState *u) {
    if(u&&u->home&&!u->wifi_setup)return action==UI_HOME_WIFI||(action==UI_HOME_BREW&&u->home_scale)||(action==UI_HOME_COLOR&&u->home_color);
    if((u&&u->wifi_setup)||s.stage==CW_HOME||(!s.scale_ready&&s.stage==CW_BREWER))return action==1;
    if(s.interrupted||!s.scale_ready)return false;
    if(action==1) {
        if(s.stage==CW_BREWER)return true;
        if(s.stage==CW_EMPTY)return !s.initial_step&&!s.stable.active&&!s.write_pending;
        // Brewing has no Stop action: lifting the dripper ends the brew.
        // A brew revisited after Stop keeps a Continue action back to beverage weighing.
        if(s.stage==CW_BREW)return s.result_valid&&!s.write_pending;
    }
    return false;
}
int ui_hit(const cw_flow_t &s,int x,int y,const UiState *u) {
    if((x-234)*(x-234)+(y-233)*(y-233)>233*233)return 0;
    if(u&&u->home&&!u->wifi_setup) {
        int hit=y>=design::button_top?UI_HOME_WIFI:(x>=74&&x<394&&y>=160&&y<232)?UI_HOME_BREW:(x>=74&&x<394&&y>=248&&y<320)?UI_HOME_COLOR:0;
        return hit&&ui_enabled(s,hit,u)?hit:0;
    }
    return y>=design::button_top&&ui_enabled(s,1,u)?1:0;
}
// Probe temperature curve since the pour, with the added-water steps behind it.
// Fixed 60-100 C axis; time axis per cw_trace_span_ms. Brewing shows a compact live
// band (no labels); the result page shows the same frozen trace that the Dropbox
// CSV holds, from the pour to the dripper lift, with axis and time labels.
// Returns the elapsed brew time of the newest sample.
static uint32_t trace_chart(int X0,int X1,int Y0,int Y1,bool live) {
    constexpr int MAX_COLUMNS=304;
    constexpr float TMIN=60,TMAX=100;
    static int16_t probe[MAX_COLUMNS];static int32_t weight[MAX_COLUMNS];
    const int columns=std::min(X1-X0,MAX_COLUMNS);X1=X0+columns;
    uint32_t elapsed=0,span=0;
    size_t n=probe_trace_chart(columns,probe,weight,&elapsed,&span);
    const bool labels=!live;
    char b[24];
    auto y_of=[&](float celsius){return (int)std::lround(Y1-(Y1-Y0)*(std::clamp(celsius,TMIN,TMAX)-TMIN)/(TMAX-TMIN));};
    for(int deg=70;deg<=100;deg+=10) {int y=y_of(deg);c->drawFastHLine(X0,y,X1-X0,LINE);if(labels)fmt(X0-22,y,18,MUTED,"%d",deg);}
    float now_x=X0+(float)(X1-X0)*elapsed/span;
    for(uint32_t t=0;t<=span;t+=60000) {
        int x=X0+(int)std::lround((float)(X1-X0)*t/span);
        c->drawFastVLine(x,Y0,Y1-Y0,LINE);
        if(labels&&(!n||std::fabs(x-now_x)>26))fmt(x,Y1+16,18,MUTED,"%u:00",(unsigned)(t/60000));
    }
    int32_t scale_cg=30000;
    for(size_t i=0;i<n;i++)if(weight[i]!=CW_TRACE_NO_WEIGHT&&weight[i]/100*115>scale_cg)scale_cg=weight[i]/100*115;
    // Two scales: the beverage band sits in front of the water steps on the same axis.
    static int32_t out[MAX_COLUMNS];
    size_t n_out=cu&&cu->dual.active?std::min(n,probe_trace_out_chart(columns,out)):0;
    int last_top=-1,last_x=X0;int32_t last_weight=0;
    for(size_t i=0;i<n;i++) {
        if(weight[i]==CW_TRACE_NO_WEIGHT)continue;
        int32_t w=std::max<int32_t>(0,weight[i]);int h=(int)((Y1-Y0)*.92f*w/scale_cg),x=X0+(int)i,top=Y1-h;
        if(h>0)c->drawFastVLine(x,top,h,design::water_area);
        if(last_top>=0)c->drawLine(x-1,last_top,x,top,design::water_edge);
        last_top=top;last_x=x;last_weight=w;
    }
    int out_top=-1,out_x=X0;int32_t out_last=0;
    for(size_t i=0;i<n_out;i++) {
        if(out[i]==CW_TRACE_NO_WEIGHT)continue;
        int32_t w=std::max<int32_t>(0,out[i]);int h=(int)((Y1-Y0)*.92f*w/scale_cg),x=X0+(int)i,top=Y1-h;
        if(h>0)c->drawFastVLine(x,top,h,design::out_area);
        if(out_top>=0)c->drawLine(x-1,out_top,x,top,design::out_edge);
        out_top=top;out_x=x;out_last=w;
    }
    if(labels&&last_weight>=100&&last_x-X0>70) {
        snprintf(b,sizeof b,"%ld ml",(long)((last_weight+50)/100));
        int water_y=Y1-(int)((Y1-Y0)*.92f*last_weight/scale_cg);
        text(b,last_x-8,n_out?water_y+14:Y1-14,18,MUTED,0,false,middle_right);
        if(n_out&&out_last>=100) {snprintf(b,sizeof b,"%ld g",(long)((out_last+50)/100));text(b,out_x-8,Y1-14,18,design::out_edge,0,false,middle_right);}
    }
    // Switch/AeroPress: where the cup scale first saw the drain (valve open, press).
    if(cu&&cu->dual.active&&cu->dual.open_valid&&span) {
        int x=X0+(int)std::lround((float)(X1-X0)*cu->dual.open_ms/span);
        for(int y=Y0;y<Y1;y+=6)c->drawFastVLine(x,y,3,design::out_edge);
        if(labels)text("Open",x,Y0-12,16,design::out_edge);
    }
    int px=0,py=0;bool have=false;float previous=0;
    auto visit=[&](size_t i) {
        if(probe[i]==CW_TRACE_NO_PROBE) {have=false;return;}
        float celsius=probe[i]/100.f;int x=X0+(int)i,y=y_of(celsius);
        // Below-axis readings (the dry bed before the pour) enter from the bottom edge.
        if(have&&!(previous<TMIN&&celsius<TMIN))c->drawWideLine(px,py,x,y,1.5f,ACCENT);
        px=x;py=y;have=true;previous=celsius;
    };
    // Two-pixel steps keep the antialiased line smooth; the newest column is always drawn.
    for(size_t i=0;i<n;i+=2)visit(i);
    if(n&&(n-1)%2)visit(n-1);
    if(live&&have&&previous>=TMIN) {c->fillSmoothCircle(px,py,7,BUTTON);c->fillSmoothCircle(px,py,4,ACCENT);}
    if(labels&&n)fmt((int)now_x,Y1+16,18,ACCENT,"%u:%02u",(unsigned)(elapsed/60000),(unsigned)(elapsed/1000%60));
    return elapsed;
}
// Right-hand metric with a small unit: the value ends at x, the unit follows it.
static void unit_value(const char *value,const char *unit,int x,int y,uint32_t color=FG) {
    text(value,x,y,40,color,0,false,middle_right);text(unit,x+7,y+6,22,MUTED,0,false,middle_left);
}
// Bed-probe reading as a 40px value; the fonts are ASCII, so the degree sign is a ring.
static void probe_value(int x,int y) {
    probe_reading_t now;probe_trace_reading(&now);
    char b[16];
    if(!now.valid) {text("--",x,y,40,MUTED,0,false,middle_right);return;}
    snprintf(b,sizeof b,"%.1f",(double)now.celsius);text(b,x,y,40,FG,0,false,middle_right);
    c->fillSmoothCircle(x+9,y-10,4,MUTED);c->fillSmoothCircle(x+9,y-10,2,BG);
    text("C",x+16,y+6,22,MUTED,0,false,middle_left);
}
static void highlights(const cw_flow_t &s,int y) {
    text("TDS",150,y,design::type::label,MUTED);text("Extraction",318,y,design::type::label,MUTED);
    fmt(150,y+42,design::type::result,ACCENT,"%.2f%%",(double)s.tds);fmt(318,y+42,design::type::result,ACCENT,"%.2f%%",(double)s.extraction);
}
// Fixed top-center telemetry, outside page-entry motion.
static void battery(const UiState &u) {
    float page_enter=enter;enter=1;
    uint32_t battery_color=u.battery_percent>=0&&u.battery_percent<=15?ACCENT:MUTED;
    c->drawRoundRect(195,24,28,14,3,battery_color);c->fillRect(224,28,3,6,battery_color);
    if(u.battery_percent>=0)c->fillRect(198,27,22*u.battery_percent/100,8,battery_color);
    if(u.battery_percent>=0)fmt(254,31,20,battery_color,"%d%%",u.battery_percent);
    else text("--%",254,31,20,battery_color);
    enter=page_enter;
}
// Compact 34px metric for three columns: value then a small unit.
static void small_metric(const char *value,const char *unit,int x,int y,uint32_t color=FG) {
    text(value,x+10,y,34,color,0,false,middle_right);text(unit,x+15,y+5,18,MUTED,0,false,middle_left);
}
void ui_draw(const cw_flow_t &s,const UiState &u) {
    c=&board_canvas();c->fillScreen(BG);cu=&u;
    battery(u);
    if(!u.wifi_setup&&u.record_visible&&(s.stage==CW_TDS||s.stage==CW_REPORT)) {
        char failed[56];snprintf(failed,sizeof failed,"Dropbox: %s",u.record.error[0]?u.record.error:"save failed");
        const char *label=u.record.state==CW_RECORD_SAVING?"Dropbox: saving":u.record.state==CW_RECORD_SAVED?"Dropbox: saved":failed;
        text(label,234,438,u.record.state==CW_RECORD_FAILED?12:16,u.record.state==CW_RECORD_SAVED?ACCENT:MUTED);
    }
    if(u.wifi_setup) {
        text("Wi-Fi setup",234,116,40);
        if(u.network.setup_active) {
            text(u.network.paired?"Browser paired":"Pairing code",234,174,22,MUTED);
            fmt(234,229,90,FG,"%06u",u.network.passkey);
        } else text(u.network.setup_error?"Bluetooth unavailable":"Opening setup",234,229,32,MUTED);
        const char *states[]={"Not configured","Connecting","Connected","Connection failed","Disconnected"};
        text(states[u.network.state],234,296,26,u.network.state==CW_NET_CONNECTED?ACCENT:MUTED);
        if(u.network.state==CW_NET_CONNECTED)text(u.network.ip,234,326,20,MUTED);
        if(u.network.dropbox_state[0]) {
            const char *dropbox="Dropbox not configured";
            if(!strcmp(u.network.dropbox_state,"saved"))dropbox="Dropbox authorization saved";
            else if(!strcmp(u.network.dropbox_state,"authorizing"))dropbox="Dropbox: authorizing";
            else if(!strcmp(u.network.dropbox_state,"verifying"))dropbox="Dropbox: checking upload";
            else if(!strcmp(u.network.dropbox_state,"connected"))dropbox="Dropbox upload verified";
            else if(!strcmp(u.network.dropbox_state,"failed"))dropbox="Dropbox: check browser";
            text(dropbox,234,354,18,MUTED);
        }
        bottom("Close",true,u.held==1);return;
    }
    if(u.home) {
        // Home: the first device to connect picks the product; a connected one is also a choice.
        text("Connect scale or color sensor",234,118,design::type::body,MUTED);
        struct Tile {const char *label,*on,*off;bool enabled;int action,top;};
        const Tile tiles[]={{"Brew",u.home_scales[0]?u.home_scales:"Scale connected","Searching for scale",u.home_scale,UI_HOME_BREW,160},
                            {"Roast color","Color sensor attached","No color sensor",u.home_color,UI_HOME_COLOR,248}};
        for(const Tile &t:tiles) {
            bool held=t.enabled&&u.held==t.action;
            if(t.enabled)c->fillSmoothRoundRect(74,t.top,320,72,18,held?ACCENT:BUTTON);
            else c->drawRoundRect(74,t.top,320,72,18,LINE);
            text(t.label,234,t.top+28,32,held?BG:(t.enabled?ACCENT:MUTED));
            text(t.enabled?t.on:t.off,234,t.top+56,18,held?BG:MUTED);
        }
        if(s.error[0]&&!u.home_scale)text(s.error,234,338,16,MUTED);
        bottom("Wi-Fi setup",true,u.held==UI_HOME_WIFI);return;
    }
    if(s.interrupted) {
        text("Brew interrupted",234,174,36);text(s.error[0]?s.error:"Scale disconnected",234,224,20,MUTED);
        text("Hold both to restart",234,303,19,ACCENT);return;
    }
    if(!s.scale_ready&&s.stage!=CW_REPORT&&s.stage!=CW_TDS) {
        // The second scale alone cannot brew: the stored dose comes from BOOKOO.
        if(s.aux_ready) {text("Connect BOOKOO",design::center_x,design::center_y,design::type::status,FG,0,true);text("Dose comes from BOOKOO",234,290,18,MUTED);bottom("Wi-Fi setup",true,u.held==1);return;}
        text("Connect scale",design::center_x,design::center_y,design::type::status,FG,0,true);bottom("Wi-Fi setup",true,u.held==1);if(s.error[0])text(s.error,234,290,18,MUTED);return;
    }
    char b[64];
    switch(s.stage) {
    case CW_HOME:text("Connect scale",design::center_x,design::center_y,design::type::status,FG,0,true);bottom("Wi-Fi setup",true,u.held==1);break;
    case CW_BREWER:
        if(u.carousel_dir) {
            for(int k=-2;k<=2;k++) {
                float offset=k-u.carousel_dir*u.carousel_progress;
                float x=234+203*offset,size=std::max(80.f,270-190*std::abs(offset));
                if(x+size/2<0||x-size/2>468)continue;
                icon((u.carousel_from+k+8)%4,x,215,size,mix(ACCENT,MUTED,std::min(255u,(unsigned)(std::abs(offset)*255))));
            }
        } else {icon((s.brewer+3)%4,31,215,80,MUTED);icon(s.brewer,234,215,270,ACCENT);icon((s.brewer+1)%4,437,215,80,MUTED);}
        bottom(names[s.brewer],!u.carousel_dir,u.held==1);break;
    case CW_EMPTY:case CW_BEVERAGE: {
        bool empty=s.stage==CW_EMPTY;
        int32_t weight=empty?s.live_cg:s.live_cg-s.empty_cg;
        const char *caption=empty?(s.initial_step?"Preparing scale":(s.stable.active?"Empty server":"Place empty server")):
            (s.stable.active?"Beverage weight":"Keep server on scale");
        if(s.stable.active)stability_bar(s.stable.progress);
        text(caption,234,146,design::type::body,MUTED);
        if(s.live_valid&&(empty||weight>0))fmt(225,236,design::type::hero,number_color(),"%.1f",weight/100.0);
        else text("--",234,236,design::type::hero,MUTED);
        text("g",360,254,25,MUTED);
        if(empty&&!s.initial_step&&!s.stable.active)bottom("Reset",ui_enabled(s,1),u.held==1);
        break;
    }
    case CW_PREP:
        if(s.live_valid)fmt(225,228,design::type::hero,number_color(),"%.1f",s.live_cg/100.0);else text("--",234,228,design::type::hero,MUTED);
        text("g",360,249,25,MUTED);
        // Ready arms itself once the dose is known; without it the brew cannot start.
        text("Save dose on scale",234,146,design::type::body,MUTED);break;
    case CW_READY:
        // Armed by itself: no Start action. Dose and probe confirm the brew can start.
        text("Pour to start",234,124,design::type::body,MUTED);
        text("Ready",234,190,design::type::ready,mix(FG,ACCENT,150+(unsigned)(breath*105)));
        text("Dose",150,266,design::type::label,MUTED);text("Bed",318,266,design::type::label,MUTED);
        snprintf(b,sizeof b,"%.1f",s.scale.dose_cg/100.0);unit_value(b,"g",170,300);
        probe_value(336,300);
        if(u.dual.scales>=2)text("2 scales",234,66,18,MUTED);
        break;
    case CW_BREW: {
        // Water leads, ratio and bed temperature follow, the clock uses the free bottom.
        // Water changes every notification while pouring, so it never value-flashes.
        bool stopped=s.result_valid;
        int32_t water=stopped?s.result.water_cg:cw_brew_water(&s);
        if(stopped||s.live_valid)fmt(222,124,design::type::hero,FG,"%.1f",water/100.0);else text("--",234,124,design::type::hero,MUTED);
        text("ml",344,142,design::type::body,MUTED,0,false,middle_left);
        bool ratio=s.brew_dose_valid&&s.brew_dose_cg>0;
        if(!stopped&&u.dual.active) {
            // Two scales: Ratio, bed temperature and the cup scale's drip (out) flow.
            text("2 scales",234,66,18,MUTED);
            text("Ratio",120,194,design::type::label,MUTED);text("Bed",234,194,design::type::label,MUTED);text("Out",348,194,design::type::label,MUTED);
            if(ratio)fmt(120,228,34,ACCENT,"1:%.1f",(double)water/s.brew_dose_cg);else text("1:--",120,228,34,MUTED);
            probe_reading_t now;probe_trace_reading(&now);
            if(now.valid) {
                snprintf(b,sizeof b,"%.1f",(double)now.celsius);text(b,242,228,34,FG,0,false,middle_right);
                c->fillSmoothCircle(249,219,3,MUTED);c->fillSmoothCircle(249,219,1,BG);text("C",254,233,18,MUTED,0,false,middle_left);
            } else text("--",234,228,34,MUTED);
            if(u.dual.out_valid) {snprintf(b,sizeof b,"%.1f",(double)u.dual.out_gps);small_metric(b,"g/s",340,228,design::out_edge);}
            else text("--",348,228,34,MUTED);
            uint32_t elapsed=trace_chart(88,380,264,334,true);
            time_text(elapsed,234,396,design::type::time);
            break;
        }
        text("Ratio",150,194,design::type::label,MUTED);text(stopped?"Dose":"Bed",318,194,design::type::label,MUTED);
        if(ratio)fmt(150,228,40,ACCENT,"1:%.1f",(double)water/s.brew_dose_cg);
        else text("1:--",150,228,40,MUTED);
        if(stopped) {
            snprintf(b,sizeof b,"%.1f",s.result.dose_cg/100.0);unit_value(b,"g",338,228);
            time_text(s.result.elapsed_ms,234,296,design::type::time,ACCENT);
            bottom("Continue",ui_enabled(s,1),u.held==1);
        } else {
            probe_value(336,228);
            uint32_t elapsed=trace_chart(88,380,264,334,true);
            time_text(elapsed,234,396,design::type::time);
        }
        break;
    }
    case CW_TDS:
        if(ui_curve_page(s,u)) {trace_chart(88,392,118,318,false);break;}
        snprintf(b,sizeof b,"%.1fg",s.result.dose_cg/100.0);field("Coffee dose",b,151,143,39);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Brew ratio",b,317,143,39);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,151,246,39);
        text("Brew time",317,246,design::type::label,MUTED);time_text(s.result.elapsed_ms,317,280,39);
        text("Waiting TDS",234,365,34);
        if(!s.rs_ready)text("RS Disconnected",234,406,20,MUTED);
        else if(s.error[0])text(s.error,234,406,20,MUTED);
        break;
    case CW_REPORT:
        if(ui_curve_page(s,u)) {trace_chart(88,392,118,318,false);break;}
        if(u.result_page==1) {
            auto xx=[](float v){return 75+(v-14)*318/12;};
            auto yy=[](float v){return 307-(v-.8f)*226;};
            for(float y=1;y<1.81f;y+=.2f) {int py=yy(y);c->drawFastHLine(75,py,318,LINE);fmt(94,py+12,15,MUTED,"%.1f",(double)y);}
            for(int x: {14,18,22,26}) {int px=xx(x);c->drawFastVLine(px,81,226,LINE);fmt(px+(x==26?-11:11),294,15,MUTED,"%d",x);}
            int l=xx(18),r=xx(22),top=yy(1.35),bottom_y=yy(1.15);
            for(int x=l;x<r;x+=10) {c->drawFastHLine(x,top,std::min(5,r-x),ACCENT);c->drawFastHLine(x,bottom_y,std::min(5,r-x),ACCENT);}
            for(int y=top;y<bottom_y;y+=10) {c->drawFastVLine(l,y,std::min(5,bottom_y-y),ACCENT);c->drawFastVLine(r,y,std::min(5,bottom_y-y),ACCENT);}
            int px=xx(std::clamp(s.extraction,14.f,26.f)),py=yy(std::clamp(s.tds,.8f,1.8f));
            c->fillCircle(px,py,8,BUTTON);c->fillCircle(px,py,4,ACCENT);highlights(s,352);break;
        }
        time_text(s.result.elapsed_ms,234,88,design::type::time);
        snprintf(b,sizeof b,"%.1fg",s.result.dose_cg/100.0);field("Coffee",b,127,164,32,100);
        snprintf(b,sizeof b,"%.1fml",s.result.water_cg/100.0);field("Water",b,234,164,32,100);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,341,164,32,100);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Ratio",b,155,260,34);
        snprintf(b,sizeof b,"%.2fg/g",(double)s.retention);field("Retention",b,313,260,32);
        highlights(s,354);break;
    }
}

// Roast-color mode. The caption sits at y=124, the 90px index at y=214, the
// readings of the current group at y=286, the stability or calibration bar at
// y=306 and one status line at y=332, above the bottom action.
const char *ui_color_action(const ColorView &v,bool *enabled) {
    bool on=true;const char *label="Calibrate";
    switch(v.page) {
    case COLOR_WARMUP:case COLOR_NO_DATA:on=false;break;
    case COLOR_CAL_PLACE:case COLOR_CAL_SETTLE:case COLOR_CAL_STEP:label="Cancel";break;
    case COLOR_CAL_DONE:case COLOR_CAL_FAILED:label="Done";break;
    default:break;
    }
    if(enabled)*enabled=on;
    return label;
}
int ui_color_hit(const ColorView &v,int x,int y) {
    bool on;ui_color_action(v,&on);
    if((x-234)*(x-234)+(y-233)*(y-233)>233*233)return 0;
    return y>=design::button_top&&on?1:0;
}
bool ui_color_tick(const ColorView &v,int64_t now) {
    int page=200+(int)v.page;
    if(page!=previous_page) {previous_page=page;entered_us=now;}
    enter=std::clamp((now-entered_us)/(float)design::motion::enter_us,0.f,1.f);
    enter=1-std::pow(1-enter,3);
    return enter<1;
}
void ui_color_draw(const ColorView &v,const UiState &u) {
    c=&board_canvas();c->fillScreen(BG);battery(u);
    char b[48];
    auto hero=[&](bool valid,float value,uint32_t color) {
        if(valid)fmt(234,214,design::type::hero,color,"%.1f",(double)value);else text("--",234,214,design::type::hero,MUTED);
    };
    auto readings=[&]() {
        int n=0;b[0]=0;
        for(unsigned i=0;i<v.count&&i<3;i++)n+=snprintf(b+n,sizeof b-n,i?"   %.1f":"%.1f",(double)v.readings[i]);
        // A single reading is already the hero value.
        if(v.count>1||(v.count&&v.page==COLOR_MEASURING))text(b,234,286,design::type::body,MUTED);
    };
    auto calibration=[&]() {
        if(v.calibrated) {snprintf(b,sizeof b,"Baking soda %.1f, LED %u",(double)v.soda,v.pa);text(b,234,332,20,MUTED);}
        else text("Not calibrated",234,332,20,ACCENT);
    };
    bool on;const char *action=ui_color_action(v,&on);
    // Upload state of the current group, under the battery.
    if(v.sync_visible) {
        const char *label=v.sync_unconfigured?"Dropbox not set up":v.sync==CW_RECORD_SAVING?"Dropbox: saving":v.sync==CW_RECORD_SAVED?"Dropbox: saved":"Dropbox: save failed";
        text(label,234,66,18,v.sync==CW_RECORD_SAVED?ACCENT:MUTED);
    }
    switch(v.page) {
    case COLOR_WARMUP:
        text("Warming up",234,124,design::type::body,MUTED);
        {unsigned seconds=(v.warmup_ms+999)/1000;fmt(234,214,design::type::hero,FG,"%u:%02u",seconds/60,seconds%60);}
        calibration();break;
    case COLOR_PLACE:
        text(v.count?"Place again":"Place on sample",234,124,design::type::body,MUTED);
        hero(v.count>0,v.count?v.readings[v.count-1]:0,ACCENT);readings();
        if(v.count) {snprintf(b,sizeof b,"Reading %u of 3",v.count);text(b,234,332,20,MUTED);} else calibration();
        break;
    case COLOR_MEASURING:
        text("Measuring",234,124,design::type::body,MUTED);
        hero(v.live_valid,v.live,FG);readings();stability_bar(v.progress);break;
    case COLOR_READING:
        text("Lift and place again",234,124,design::type::body,MUTED);
        hero(true,v.readings[v.count-1],ACCENT);readings();
        snprintf(b,sizeof b,"Reading %u of 3",v.count);text(b,234,332,20,MUTED);break;
    case COLOR_RESULT:
        text("Roast color",234,124,design::type::body,MUTED);
        hero(true,v.mean,ACCENT);readings();
        snprintf(b,sizeof b,v.retest?"Retest: SD %.1f":"SD %.1f",(double)v.sd);text(b,234,332,20,v.retest?ACCENT:MUTED);break;
    case COLOR_SATURATED:
        text("Too bright",234,124,design::type::body,MUTED);hero(false,0,FG);
        text("Calibrate on baking soda",234,332,20,ACCENT);break;
    case COLOR_NO_DATA:
        text("Color sensor",234,124,design::type::body,MUTED);hero(false,0,FG);
        text("No sensor data",234,332,20,ACCENT);break;
    case COLOR_CAL_PLACE:
        text("Calibration",234,124,design::type::body,MUTED);
        text("Place on baking soda",234,214,design::type::button);
        text("Reads 230 when done",234,332,20,MUTED);break;
    case COLOR_CAL_SETTLE:case COLOR_CAL_STEP:
        text(v.page==COLOR_CAL_STEP?"Calibrating":"Calibration",234,124,design::type::body,MUTED);
        text("Hold still",234,214,design::type::button);stability_bar(v.progress);break;
    case COLOR_CAL_DONE:
        text("Calibrated",234,124,design::type::body,MUTED);hero(true,v.cal_index,ACCENT);
        snprintf(b,sizeof b,"Baking soda, LED %u",v.pa);text(b,234,332,20,MUTED);break;
    case COLOR_CAL_FAILED:
        text("Calibration failed",234,124,design::type::body,MUTED);
        text(v.failure==COLOR_FAIL_TOO_DARK?"Too dark for 230":"Sensor moved",234,214,design::type::button);
        snprintf(b,sizeof b,"Kept LED %u",v.pa);text(b,234,332,20,MUTED);break;
    }
    bottom(action,on,u.held==1);
}
