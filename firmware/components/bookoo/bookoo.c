// SPDX-License-Identifier: MIT
#include "bookoo.h"
#include <string.h>

static uint32_t be24(const uint8_t *p)
{ return ((uint32_t)p[0]<<16) | ((uint32_t)p[1]<<8) | p[2]; }
static bool signed_value(uint8_t sign, uint32_t value, int32_t *out)
{
    if(sign!=0x2b && sign!=0x2d)return false;
    *out=sign==0x2d?-(int32_t)value:(int32_t)value;
    return true;
}
bookoo_parse_t bookoo_decode(const uint8_t *d,size_t n,bookoo_message_t *out)
{
    if(!d || !out || n!=20 || d[0]!=3)return BOOKOO_BAD_LENGTH;
    uint8_t checksum=0;for(size_t i=0;i<n;i++)checksum^=d[i];
    if(checksum)return BOOKOO_BAD_CHECKSUM;
    bookoo_message_t m={.type=d[1]};
    if(d[1]==0x0b) {
        m.kind=BOOKOO_WEIGHT;m.elapsed_ms=be24(d+2);m.display_unit=d[5];m.battery=d[13];
        if(!signed_value(d[6],be24(d+7),&m.weight_cg) ||
           !signed_value(d[10],((uint32_t)d[11]<<8)|d[12],&m.flow_cg_s))return BOOKOO_BAD_SIGN;
    } else if(d[1]==0x10) {
        // Official BOOKOO N getFeatureInfo (14) snapshot: stored dose bytes 6..8.
        m.kind=BOOKOO_DOSE;m.dose_cg=(int32_t)be24(d+6);
    } else if(d[1]==0x0f) {
        m.kind=BOOKOO_DOSE;
        if(!signed_value(d[2],be24(d+3),&m.dose_cg))return BOOKOO_BAD_SIGN;
    } else if(d[1]==0x0d) {
        m.kind=BOOKOO_AUTO;m.state=d[2];m.elapsed_ms=be24(d+3);
        if(m.state>4)return BOOKOO_BAD_STATE;
        if(!signed_value(d[6],be24(d+7),&m.weight_cg) ||
           !signed_value(d[10],((uint32_t)d[11]<<8)|d[12],&m.settlement_hundredths))return BOOKOO_BAD_SIGN;
    }
    *out=m;return BOOKOO_PARSE_OK;
}
void bookoo_session_init(bookoo_session_t *s)
{ memset(s,0,sizeof *s); }
void bookoo_session_disconnect(bookoo_session_t *s)
{
    s->connected=false;s->pending=BOOKOO_CMD_NONE;s->live_valid=false;s->dose_valid=false;
    if(s->phase==BOOKOO_READY || s->phase==BOOKOO_RUNNING)s->phase=BOOKOO_INTERRUPTED;
}
void bookoo_session_connect(bookoo_session_t *s,uint32_t epoch)
{
    bookoo_session_disconnect(s);
    s->epoch=epoch;s->connected=true;
}
void bookoo_session_begin(bookoo_session_t *s)
{
    s->brew_id++;s->phase=BOOKOO_IDLE;s->running_dose_valid=false;
    // The scale owns the stored dose. Keep its explicit current-connection
    // report across UI/new-brew navigation; disconnect invalidates it.
    if(!s->connected)s->dose_valid=false;
    s->result_valid=false;s->elapsed_ms=0;s->pending=BOOKOO_CMD_NONE;
}
static unsigned confirm(bookoo_session_t *s,bookoo_command_t command)
{
    if(s->pending!=command)return BOOKOO_UPDATE_NONE;
    s->pending=BOOKOO_CMD_NONE;return BOOKOO_UPDATE_COMMAND;
}
unsigned bookoo_session_receive(bookoo_session_t *s,uint32_t epoch,const bookoo_message_t *m)
{
    if(!s->connected || epoch!=s->epoch)return BOOKOO_UPDATE_NONE;
    unsigned update=0;
    if(m->kind==BOOKOO_WEIGHT) {
        s->live_weight_cg=m->weight_cg;s->live_valid=true;update|=BOOKOO_UPDATE_WEIGHT;
        if(s->phase==BOOKOO_RUNNING) {
            s->elapsed_ms=m->elapsed_ms;
        }
    } else if(m->kind==BOOKOO_DOSE) {
        // This is the scale-owned current stored value, independent of UI phase.
        // running_dose_cg is a separate immutable snapshot taken at Start.
        s->dose_cg=m->dose_cg;s->dose_valid=m->dose_cg>0 && m->dose_cg<=99990;
        update|=BOOKOO_UPDATE_DOSE;
    } else if(m->kind==BOOKOO_AUTO) {
        switch(m->state) {
        case 2:
            if(s->phase==BOOKOO_IDLE) {s->phase=BOOKOO_READY;s->elapsed_ms=0;update|=BOOKOO_UPDATE_STATE|confirm(s,BOOKOO_CMD_READY);}
            break;
        case 1:
            if(s->phase==BOOKOO_READY) {
                s->phase=BOOKOO_RUNNING;s->elapsed_ms=0;
                s->running_dose_cg=s->dose_cg;s->running_dose_valid=s->dose_valid;
                update|=BOOKOO_UPDATE_STATE|confirm(s,BOOKOO_CMD_START);
            }
            break;
        case 0:
            if(s->phase==BOOKOO_RUNNING) {
                s->result=(bookoo_result_t){.brew_id=s->brew_id,.elapsed_ms=m->elapsed_ms,
                    .dose_cg=s->running_dose_valid?s->running_dose_cg:0,.dose_valid=s->running_dose_valid,
                    .water_cg=m->weight_cg,.water_valid=m->weight_cg>=0};
                s->result_valid=true;s->phase=BOOKOO_STOPPED;s->elapsed_ms=m->elapsed_ms;
                update|=BOOKOO_UPDATE_STATE|BOOKOO_UPDATE_RESULT|confirm(s,BOOKOO_CMD_STOP);
            }
            break;
        case 3:
            if(s->phase==BOOKOO_READY) {s->phase=BOOKOO_IDLE;update|=BOOKOO_UPDATE_STATE|confirm(s,BOOKOO_CMD_EXIT_READY);}
            break;
        case 4:
            // Leaving the scale's result screen does not erase our frozen result.
            // The app must explicitly begin the next brew.
            break;
        }
    }
    return update;
}
bool bookoo_session_request(bookoo_session_t *s,bookoo_command_t c,
                            uint8_t p[6])
{
    if(!s->connected || s->pending || !p)return false;
    if(c==BOOKOO_CMD_READY) {
        if(s->phase!=BOOKOO_IDLE || !s->dose_valid)return false;
    } else if(c==BOOKOO_CMD_EXIT_READY) {
        if(s->phase!=BOOKOO_READY)return false;
    } else if(c==BOOKOO_CMD_START) {
        if(s->phase!=BOOKOO_READY || !s->dose_valid)return false;
    } else if(c==BOOKOO_CMD_STOP) {
        if(s->phase!=BOOKOO_RUNNING)return false;
    } else return false;
    p[0]=3;p[1]=10;p[2]=(uint8_t)c;p[3]=c==BOOKOO_CMD_EXIT_READY?1:0;p[4]=0;
    p[5]=p[0]^p[1]^p[2]^p[3];
    s->pending=c;return true;
}
void bookoo_session_write_failed(bookoo_session_t *s)
{ s->pending=BOOKOO_CMD_NONE; }
