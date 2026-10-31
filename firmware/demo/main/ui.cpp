// SPDX-License-Identifier: MIT
#include "ui.h"
#include "icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
static constexpr uint32_t BG=0x141c18,FG=0xe9eee8,MUTED=0x8d9e93,ACCENT=0xc9f27b,LINE=0x334038,BUTTON=0x26352a;
static const char *names[]={"V60","Switch","AeroPress","Hoop"};
static LGFX_Sprite *c;
static void text(const char *s,int x,int y,int px,uint32_t color=FG) {
    c->setFont(px>=36?&fonts::FreeSans24pt7b:&fonts::FreeSans12pt7b);
    c->setTextSize((float)px/(px>=36?48:24));c->setTextDatum(middle_center);c->setTextColor(color);c->drawString(s,x,y);
}
static void fmt(int x,int y,int px,uint32_t color,const char *format,...) {
    char b[80];va_list a;va_start(a,format);vsnprintf(b,sizeof b,format,a);va_end(a);text(b,x,y,px,color);
}
static void time_text(uint32_t ms,int x,int y,int size) {fmt(x,y,size,FG,"%u:%02u",(unsigned)(ms/60000),(unsigned)(ms/1000%60));}
static void field(const char *label,const char *value,int x,int y,int size=32) {text(label,x,y,18,MUTED);text(value,x,y+34,size);}
static void bottom(const char *label,bool enabled,bool held) {
    c->fillRect(0,354,c->width(),112,held&&enabled?ACCENT:BUTTON);c->drawFastHLine(0,354,c->width(),LINE);
    text(label,234,393,30,held&&enabled?BG:(enabled?ACCENT:MUTED));
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
bool ui_enabled(const cw_flow_t &s,int action) {
    if(s.interrupted||!s.scale_ready)return false;
    if(action==1) {
        if(s.stage==CW_BREWER)return true;
        if(s.stage==CW_EMPTY)return !s.stable.active&&!s.write_pending;
        if(s.stage==CW_BREW)return !s.write_pending&&!s.scale.pending;
    }
    if(s.stage==CW_PREP&&action==1&&!s.write_pending&&!s.scale.pending)
        return s.scale.phase==BOOKOO_IDLE&&s.scale.dose_valid&&s.empty_valid;
    return false;
}
int ui_hit(const cw_flow_t &s,int x,int y) {
    if((x-234)*(x-234)+(y-233)*(y-233)>233*233)return 0;
    return y>=354&&ui_enabled(s,1)?1:0;
}
static void highlights(const cw_flow_t &s,int y) {
    text("TDS",150,y,18,MUTED);text("Extraction",318,y,18,MUTED);
    fmt(150,y+39,39,ACCENT,"%.2f%%",(double)s.tds);fmt(318,y+39,39,ACCENT,"%.2f%%",(double)s.extraction);
}
void ui_draw(const cw_flow_t &s,const UiState &u) {
    c=&board_canvas();c->fillScreen(BG);
    if(s.interrupted) {
        text("Brew interrupted",234,174,32);text(s.error[0]?s.error:"Scale disconnected",234,224,20,MUTED);
        text("Hold both to restart",234,303,19,ACCENT);return;
    }
    if(!s.scale_ready&&s.stage!=CW_REPORT&&s.stage!=CW_CHART&&s.stage!=CW_TDS) {
        text("Connect scale",234,226,40);if(s.error[0])text(s.error,234,290,18,MUTED);return;
    }
    char b[64];
    switch(s.stage) {
    case CW_HOME:text("Connect scale",234,226,40);break;
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
        const char *caption=empty?(s.stable.active?"Empty server":"Place empty server"):
            (s.stable.active?"Beverage weight":"Remove server");
        if(s.stable.active) {
            c->drawArc(234,233,210,207,0,360,LINE);
            c->drawArc(234,233,210,207,270,270+360*std::min(1.f,s.stable.progress),ACCENT);
        }
        text(caption,234,155,22,MUTED);
        if(s.live_valid&&(empty||(s.live_cg<-3000&&weight>0)))fmt(225,236,76,FG,"%.1f",weight/100.0);
        else text("--",234,236,76,MUTED);
        text("g",360,254,25,MUTED);
        if(empty&&!s.stable.active)bottom("Reset",ui_enabled(s,1),u.held==1);
        break;
    }
    case CW_PREP:
        if(s.live_valid)fmt(225,228,76,FG,"%.1f",s.live_cg/100.0);else text("--",234,228,76,MUTED);
        text("g",360,249,25,MUTED);
        bottom(s.scale.pending==BOOKOO_CMD_READY?"Starting...":"Start",ui_enabled(s,1),u.held==1);break;
    case CW_READY:
        text("Ready",234,226,48,ACCENT);break;
    case CW_BREW: {
        int32_t water=s.scale.phase==BOOKOO_STOPPED?s.scale.result.water_cg:s.live_cg;
        text("Water",234,130,22,MUTED);fmt(213,206,86,FG,"%.1f",water/100.0);text("ml",355,229,24,MUTED);
        if(s.scale.running_dose_valid)fmt(234,292,40,FG,"1:%.1f",(double)water/s.scale.running_dose_cg);else text("1:--",234,292,40,MUTED);
        bottom(s.scale.pending==BOOKOO_CMD_STOP?"Stopping...":"Stop",ui_enabled(s,1),u.held==1);break;
    }
    case CW_TDS:
        snprintf(b,sizeof b,"%.1fg",s.scale.result.dose_cg/100.0);field("Coffee dose",b,151,143,33);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Brew ratio",b,317,143,33);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,151,246,33);
        text("Brew time",317,246,18,MUTED);time_text(s.scale.result.elapsed_ms,317,280,33);
        text("Waiting TDS",234,359,28);
        if(!s.rs_ready)text("RS Disconnected",234,396,16,MUTED);
        else if(s.error[0])text(s.error,234,396,16,MUTED);
        break;
    case CW_REPORT:
        time_text(s.scale.result.elapsed_ms,234,93,57);
        snprintf(b,sizeof b,"%.1fg",s.scale.result.dose_cg/100.0);field("Coffee",b,127,164,30);
        snprintf(b,sizeof b,"%.1fml",s.scale.result.water_cg/100.0);field("Water",b,234,164,30);
        snprintf(b,sizeof b,"%.1fg",s.beverage_cg/100.0);field("Beverage",b,341,164,30);
        snprintf(b,sizeof b,"1:%.1f",(double)s.ratio);field("Ratio",b,155,260,29);
        snprintf(b,sizeof b,"%.2fg/g",(double)s.retention);field("Retention",b,313,260,29);
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
