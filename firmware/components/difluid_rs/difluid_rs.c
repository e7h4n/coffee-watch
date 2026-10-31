// SPDX-License-Identifier: MIT
#include "difluid_rs.h"
#include <math.h>
#include <string.h>

enum { WAIT_HEAD, HEAD_SECOND, BODY, ESCAPED, TAIL_SECOND };
static uint16_t u16(const uint8_t *p) {return (uint16_t)p[0] | (uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;}
static float f32(const uint8_t *p) {uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f;}
static uint16_t crc16(const uint8_t *p,size_t n) {
    uint16_t crc=0xffff;
    for(size_t i=0;i<n;i++) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xa001:0);
    }
    return crc;
}
void rs_stream_reset(rs_stream_t *s) {memset(s,0,sizeof *s);}
static void reject(rs_emit_fn emit,void *ctx,rs_error_t error) {
    rs_event_t e={.kind=RS_FRAME_REJECTED,.error=error};emit(ctx,&e);
}
static void complete(rs_stream_t *s,rs_decrypt_fn decrypt,void *dc,rs_emit_fn emit,void *ec) {
    size_t n=s->length;
    if(n<18) {reject(emit,ec,RS_LENGTH);return;}
    uint8_t sum=0;for(size_t i=0;i<n-1;i++)sum+=s->body[i];
    if(sum!=s->body[n-1]) {reject(emit,ec,RS_CHECKSUM);return;}
    if((s->body[n-2]&3)!=1) {reject(emit,ec,RS_LEVEL);return;}
    size_t cipher_n=n-2;
    if(cipher_n%16 || !decrypt(dc,s->body,s->plain,cipher_n)) {reject(emit,ec,RS_CIPHER);return;}
    const uint8_t *p=s->plain;
    size_t size=u16(p+4),end=6+size;
    if(end+2>cipher_n || cipher_n-end-2>=16) {reject(emit,ec,RS_LENGTH);return;}
    if(crc16(p,end)!=u16(p+end)) {reject(emit,ec,RS_CRC);return;}
    if(memcmp(p,"\x01\x03\x12\x00",4)) {
        rs_event_t e={.kind=RS_ROUTE_UNSUPPORTED};emit(ec,&e);return;
    }
    if(size<30) {reject(emit,ec,RS_LENGTH);return;}
    const uint8_t *d=p+6;
    rs_event_t e={.kind=RS_MEASUREMENT,.error_code=d[0],.count=d[9],
        .error_flags=u32(d+14),.tds_percent=f32(d+1),.brix_avg=f32(d+5),
        .temperature_c=f32(d+10),.refraction=f32(d+18),
        .x_pos_glue=f32(d+22),.x_pos_liquid=f32(d+26),.extra_bytes=(uint16_t)(size-30)};
    if(!isfinite(e.tds_percent)||!isfinite(e.brix_avg)||!isfinite(e.temperature_c)||
       !isfinite(e.refraction)||!isfinite(e.x_pos_glue)||!isfinite(e.x_pos_liquid)) {
        reject(emit,ec,RS_NONFINITE);return;
    }
    if(e.error_code||e.error_flags)e.kind=RS_MEASUREMENT_ERROR;
    emit(ec,&e);
}
static void append(rs_stream_t *s,uint8_t b,rs_emit_fn emit,void *ctx) {
    if(s->length==sizeof s->body) {
        s->state=WAIT_HEAD;s->length=0;reject(emit,ctx,RS_OVERFLOW);return;
    }
    s->body[s->length++]=b;
}
void rs_stream_feed(rs_stream_t *s,const uint8_t *bytes,size_t n,
                    rs_decrypt_fn decrypt,void *dc,rs_emit_fn emit,void *ec) {
    for(size_t i=0;i<n;i++) {
        uint8_t b=bytes[i];
        switch(s->state) {
        case WAIT_HEAD: if(b==0xda)s->state=HEAD_SECOND;break;
        case HEAD_SECOND:
            if(b==0xda) {s->state=BODY;s->length=0;}else s->state=WAIT_HEAD;
            break;
        case BODY:
            if(b==0xfa)s->state=ESCAPED;
            else if(b==0x0a)s->state=TAIL_SECOND;
            else if(b==0xda) {reject(emit,ec,RS_BOUNDARY);s->state=HEAD_SECOND;s->length=0;}
            else append(s,b,emit,ec);
            break;
        case ESCAPED:
            s->state=BODY;
            if(b==0xfa||b==0xda||b==0x0a)append(s,b,emit,ec);
            else {s->state=WAIT_HEAD;s->length=0;reject(emit,ec,RS_ESCAPE);}
            break;
        case TAIL_SECOND:
            if(b==0x0a)complete(s,decrypt,dc,emit,ec);
            else reject(emit,ec,RS_BOUNDARY);
            s->length=0;s->state=(b==0xda)?HEAD_SECOND:WAIT_HEAD;
            break;
        }
    }
}
