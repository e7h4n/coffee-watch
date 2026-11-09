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
static void time_text(uint32_t ms,int x,int y,int size) {fmt(x,y,size,FG,"%u:%02u",(unsigned)(ms/60000),(unsigned)(ms/1000%60));}
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
bool ui_tick(const cw_flow_t &s,int64_t now) {
    int page=s.interrupted?100:(!s.scale_ready&&s.stage!=CW_TDS&&s.stage!=CW_REPORT&&s.stage!=CW_CHART?101:(int)s.stage);
    if(page!=previous_page) {previous_page=page;entered_us=now;have_value=false;}
    enter=std::clamp((now-entered_us)/(float)design::motion::enter_us,0.f,1.f);
    enter=1-std::pow(1-enter,3);
    int32_t value=s.stage==CW_BEVERAGE?-s.live_cg-s.empty_cg:s.live_cg;
    if(!have_value||value!=previous_value) {previous_value=value;changed_us=now;have_value=true;}
    bool numeric=s.stage==CW_EMPTY||s.stage==CW_PREP||s.stage==CW_BREW||s.stage==CW_BEVERAGE;
    value_flash=numeric?1-std::clamp((now-changed_us)/(float)design::motion::value_us,0.f,1.f):0;
    breath=.5f-.5f*std::cos((now%design::motion::breath_us)*6.2831853f/design::motion::breath_us);
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
    if((u&&u->wifi_setup)||s.stage==CW_HOME||(!s.scale_ready&&s.stage==CW_BREWER))return action==1;
    if(s.interrupted||!s.scale_ready)return false;
    if(action==1) {
        if(s.stage==CW_BREWER)return true;
        if(s.stage==CW_EMPTY)return !s.initial_step&&!s.stable.active&&!s.write_pending;
        // Brewing has no Stop action: the scale ends the brew (dripper removal or its own key).
        // A brew revisited after Stop keeps a Continue action back to beverage weighing.
        if(s.stage==CW_BREW)return s.scale.phase==BOOKOO_STOPPED&&s.scale.result_valid&&!s.write_pending&&!s.scale.pending;
    }
    if(s.stage==CW_PREP&&action==1&&!s.write_pending&&!s.scale.pending)
        return s.scale.phase==BOOKOO_IDLE&&s.scale.dose_valid&&s.empty_valid;
    return false;
}
int ui_hit(const cw_flow_t &s,int x,int y,const UiState *u) {
    if((x-234)*(x-234)+(y-233)*(y-233)>233*233)return 0;
    return y>=design::button_top&&ui_enabled(s,1,u)?1:0;
}
// Brew screen: live probe temperature and its curve since the pour, with the
// added-water steps behind it. Fixed 60-100 C axis; time axis per cw_trace_span_ms.
static void brew_chart() {
    constexpr int X0=88,X1=392,Y0=142,Y1=306,COLUMNS=X1-X0;
    constexpr float TMIN=60,TMAX=100;
    static int16_t probe[COLUMNS];static int32_t weight[COLUMNS];
    uint32_t elapsed=0,span=0;
    size_t n=probe_trace_chart(COLUMNS,probe,weight,&elapsed,&span);
    probe_reading_t now;probe_trace_reading(&now);
    char b[24];
    if(now.valid) {snprintf(b,sizeof b,"%.1f",(double)now.celsius);text(b,284,96,64,FG,0,false,middle_right);}
    else text("--",284,96,64,MUTED,0,false,middle_right);
    // The bundled fonts are ASCII only: draw the degree sign as a small ring.
    c->fillSmoothCircle(292,101,4,MUTED);c->fillSmoothCircle(292,101,2,BG);
    text("C",298,108,22,MUTED,0,false,middle_left);
    auto y_of=[&](float celsius){return (int)std::lround(Y1-(Y1-Y0)*(std::clamp(celsius,TMIN,TMAX)-TMIN)/(TMAX-TMIN));};
    for(int deg=70;deg<=100;deg+=10) {int y=y_of(deg);c->drawFastHLine(X0,y,X1-X0,LINE);fmt(X0-22,y,18,MUTED,"%d",deg);}
    float now_x=X0+(float)(X1-X0)*elapsed/span;
    for(uint32_t t=0;t<=span;t+=60000) {
        int x=X0+(int)std::lround((float)(X1-X0)*t/span);
        c->drawFastVLine(x,Y0,Y1-Y0,LINE);
        if(!n||std::fabs(x-now_x)>26)fmt(x,Y1+16,18,MUTED,"%u:00",(unsigned)(t/60000));
    }
    int32_t scale_cg=30000;
    for(size_t i=0;i<n;i++)if(weight[i]!=CW_TRACE_NO_WEIGHT&&weight[i]/100*115>scale_cg)scale_cg=weight[i]/100*115;
    int last_top=-1,last_x=X0;int32_t last_weight=0;
    for(size_t i=0;i<n;i++) {
        if(weight[i]==CW_TRACE_NO_WEIGHT)continue;
        int32_t w=std::max<int32_t>(0,weight[i]);int h=(int)((Y1-Y0)*.92f*w/scale_cg),x=X0+(int)i,top=Y1-h;
        if(h>0)c->drawFastVLine(x,top,h,design::water_area);
        if(last_top>=0)c->drawLine(x-1,last_top,x,top,design::water_edge);
        last_top=top;last_x=x;last_weight=w;
    }
    if(last_weight>=100&&last_x-X0>70) {snprintf(b,sizeof b,"%ld ml",(long)((last_weight+50)/100));text(b,last_x-8,Y1-14,18,MUTED,0,false,middle_right);}
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
    if(have&&previous>=TMIN) {c->fillSmoothCircle(px,py,8,BUTTON);c->fillSmoothCircle(px,py,5,ACCENT);}
    if(n)fmt((int)now_x,Y1+16,18,ACCENT,"%u:%02u",(unsigned)(elapsed/60000),(unsigned)(elapsed/1000%60));
}
static void highlights(const cw_flow_t &s,int y) {
    text("TDS",150,y,design::type::label,MUTED);text("Extraction",318,y,design::type::label,MUTED);
    fmt(150,y+42,design::type::result,ACCENT,"%.2f%%",(double)s.tds);fmt(318,y+42,design::type::result,ACCENT,"%.2f%%",(double)s.extraction);
}
void ui_draw(const cw_flow_t &s,const UiState &u) {
    c=&board_canvas();c->fillScreen(BG);
    // Fixed top-center telemetry, outside page-entry motion.
    float page_enter=enter;enter=1;
    uint32_t battery_color=u.battery_percent>=0&&u.battery_percent<=15?ACCENT:MUTED;
    c->drawRoundRect(195,24,28,14,3,battery_color);c->fillRect(224,28,3,6,battery_color);
    if(u.battery_percent>=0)c->fillRect(198,27,22*u.battery_percent/100,8,battery_color);
    if(u.battery_percent>=0)fmt(254,31,20,battery_color,"%d%%",u.battery_percent);
    else text("--%",254,31,20,battery_color);
    enter=page_enter;
    if(!u.wifi_setup&&u.record_visible&&(s.stage==CW_TDS||s.stage==CW_REPORT||s.stage==CW_CHART)) {
        const char *label=u.record.state==CW_RECORD_SAVING?"Drive: saving":u.record.state==CW_RECORD_SAVED?"Drive: saved":"Drive: save failed";
        text(label,234,438,16,u.record.state==CW_RECORD_SAVED?ACCENT:MUTED);
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
        if(u.network.drive_state[0]) {
            const char *drive="Drive not configured";
            if(!strcmp(u.network.drive_state,"saved"))drive="Drive authorization saved";
            else if(!strcmp(u.network.drive_state,"requesting_code"))drive="Drive: requesting code";
            else if(!strcmp(u.network.drive_state,"awaiting_consent"))drive="Drive: approve in browser";
            else if(!strcmp(u.network.drive_state,"verifying"))drive="Drive: checking read/write";
            else if(!strcmp(u.network.drive_state,"connected"))drive="Drive read/write verified";
            else if(!strcmp(u.network.drive_state,"failed"))drive="Drive: check browser";
            text(drive,234,354,18,MUTED);
        }
        bottom("Close",true,u.held==1);return;
    }
    if(s.interrupted) {
        text("Brew interrupted",234,174,36);text(s.error[0]?s.error:"Scale disconnected",234,224,20,MUTED);
        text("Hold both to restart",234,303,19,ACCENT);return;
    }
    if(!s.scale_ready&&s.stage!=CW_REPORT&&s.stage!=CW_CHART&&s.stage!=CW_TDS) {
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
        int32_t weight=empty?s.live_cg:-s.live_cg-s.empty_cg;
        const char *caption=empty?(s.initial_step?"Preparing scale":(s.stable.active?"Empty server":"Place empty server")):
            (s.stable.active?"Beverage weight":"Remove server");
        if(s.stable.active)stability_bar(s.stable.progress);
        text(caption,234,146,design::type::body,MUTED);
        if(s.live_valid&&(empty||(s.live_cg<-3000&&weight>0)))fmt(225,236,design::type::hero,number_color(),"%.1f",weight/100.0);
        else text("--",234,236,design::type::hero,MUTED);
        text("g",360,254,25,MUTED);
        if(empty&&!s.initial_step&&!s.stable.active)bottom("Reset",ui_enabled(s,1),u.held==1);
        break;
    }
    case CW_PREP:
        if(s.live_valid)fmt(225,228,design::type::hero,number_color(),"%.1f",s.live_cg/100.0);else text("--",234,228,design::type::hero,MUTED);
        text("g",360,249,25,MUTED);
        bottom(s.scale.pending==BOOKOO_CMD_READY?"Starting...":"Start",ui_enabled(s,1),u.held==1);break;
    case CW_READY:
        text("Ready",234,226,design::type::ready,mix(FG,ACCENT,150+(unsigned)(breath*105)));break;
    case CW_BREW: {
        brew_chart();
        int32_t water=s.scale.phase==BOOKOO_STOPPED?s.scale.result.water_cg:s.live_cg;
        if(s.scale.phase==BOOKOO_STOPPED&&s.scale.result_valid)bottom("Continue",ui_enabled(s,1),u.held==1);
        else if(s.scale.running_dose_valid)fmt(234,362,32,FG,"1:%.1f",(double)water/s.scale.running_dose_cg);
        else text("1:--",234,362,32,MUTED);
        break;
    }
    case CW_TDS:
        snprintf(b,sizeof b,"%.1fg",s.scale.result.dose_cg/100.0);field("Coffee dose",b,151,143,39);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Brew ratio",b,317,143,39);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,151,246,39);
        text("Brew time",317,246,design::type::label,MUTED);time_text(s.scale.result.elapsed_ms,317,280,39);
        text("Waiting TDS",234,365,34);
        if(!s.rs_ready)text("RS Disconnected",234,406,20,MUTED);
        else if(s.error[0])text(s.error,234,406,20,MUTED);
        break;
    case CW_REPORT:
        time_text(s.scale.result.elapsed_ms,234,88,design::type::time);
        snprintf(b,sizeof b,"%.1fg",s.scale.result.dose_cg/100.0);field("Coffee",b,127,164,32,100);
        snprintf(b,sizeof b,"%.1fml",s.scale.result.water_cg/100.0);field("Water",b,234,164,32,100);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,341,164,32,100);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Ratio",b,155,260,34);
        snprintf(b,sizeof b,"%.2fg/g",(double)s.retention);field("Retention",b,313,260,32);
        highlights(s,354);break;
    case CW_CHART: {
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
    }
}
