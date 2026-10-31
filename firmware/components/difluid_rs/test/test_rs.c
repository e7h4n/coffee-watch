// SPDX-License-Identifier: MIT
#include "difluid_rs.h"
#include <CommonCrypto/CommonCryptor.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const uint8_t frame[]={0xda,0xda,0xcc,0x2c,0x58,0xeb,0x5f,0x5c,0x63,0xb6,0xdb,0xb8,0x8e,0x3c,0x3d,0xb1,0x87,0x70,0x18,0x98,0x64,0x04,0xe3,0x48,0x4e,0xe7,0x73,0x65,0x4d,0xa9,0x60,0x64,0x19,0xa1,0x3b,0xfa,0xfa,0xf9,0x99,0x17,0x4d,0x9f,0x47,0xd9,0x0c,0x69,0x9a,0xc5,0x0f,0x26,0x0e,0xf1,0x07,0x0a,0x0a};
static const uint8_t key[]={0x7a,0x3f,0xe9,0x1c,0x8b,0x5d,0x2a,0x6e,0x4c,0x91,0xf7,0x38,0xb2,0x64,0xd5,0x0a};
static const uint8_t iv[]={0x9e,0x2b,0x7c,0x45,0x1f,0x8a,0x3d,0x6b,0xe4,0x57,0x92,0x0c,0x8f,0x3a,0x6d,0xb1};
static unsigned results,errors,rejections;
static rs_event_t last;
static bool decrypt(void *ctx,const uint8_t *in,uint8_t *out,size_t n) {
    size_t written=0;
    return CCCrypt(kCCDecrypt,kCCAlgorithmAES,0,key,16,iv,in,n,out,n,&written)==kCCSuccess && written==n;
}
static void emit(void *ctx,const rs_event_t *e) {
    if(e->kind==RS_MEASUREMENT) {results++;last=*e;}
    else if(e->kind==RS_MEASUREMENT_ERROR)errors++;
    else rejections++;
}
static void feed(rs_stream_t *s,const uint8_t *p,size_t n) {rs_stream_feed(s,p,n,decrypt,NULL,emit,NULL);}
static uint16_t crc(const uint8_t *p,size_t n) {
    uint16_t c=0xffff;
    for(size_t i=0;i<n;i++) {c^=p[i];for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?0xa001:0);}
    return c;
}
static size_t packet(uint8_t *plain,uint8_t *wire,bool valid_crc) {
    if(valid_crc) {uint16_t c=crc(plain,45);plain[45]=c;plain[46]=c>>8;}
    uint8_t body[50];size_t written=0;
    assert(CCCrypt(kCCEncrypt,kCCAlgorithmAES,0,key,16,iv,plain,48,body,48,&written)==kCCSuccess);
    assert(written==48);body[48]=0xf1;body[49]=0;
    for(unsigned i=0;i<49;i++)body[49]+=body[i];
    size_t n=0;wire[n++]=0xda;wire[n++]=0xda;
    for(unsigned i=0;i<50;i++) {
        if(body[i]==0xfa||body[i]==0xda||body[i]==0x0a)wire[n++]=0xfa;
        wire[n++]=body[i];
    }
    wire[n++]=0x0a;wire[n++]=0x0a;return n;
}
int main(void) {
    rs_stream_t s;
    // Real encrypted capture at every possible BLE fragment boundary.
    for(size_t split=0;split<=sizeof frame;split++) {
        rs_stream_reset(&s);results=0;
        feed(&s,frame,split);
        assert(results==(split==sizeof frame));
        feed(&s,frame+split,sizeof frame-split);
        assert(results==1 && last.count==1 && last.extra_bytes==9);
        assert(fabsf(last.tds_percent-0.0054549584f)<0.0000001f);
        assert(fabsf(last.temperature_c-22.8057518f)<0.00001f);
        assert(fabsf(last.refraction-1.332999587f)<0.000001f);
    }
    // Consecutive frames and single-byte transport delivery.
    results=0;rs_stream_reset(&s);
    for(unsigned pass=0;pass<2;pass++)for(size_t i=0;i<sizeof frame;i++)feed(&s,frame+i,1);
    assert(results==2);
    // Disconnect reset discards a partial frame; no synthetic completion.
    results=0;rs_stream_reset(&s);feed(&s,frame,20);rs_stream_reset(&s);
    feed(&s,frame+20,sizeof frame-20);assert(results==0);
    feed(&s,frame,sizeof frame);assert(results==1);
    // Corruption cannot produce a result. Next valid frame works by boundaries.
    uint8_t bad[sizeof frame];memcpy(bad,frame,sizeof bad);bad[8]^=1;
    results=0;rejections=0;rs_stream_reset(&s);feed(&s,bad,sizeof bad);
    assert(results==0 && rejections==1);feed(&s,frame,sizeof frame);assert(results==1);
    // Oversize notification sequence is bounded and explicitly rejected.
    uint8_t huge[RS_FRAME_CAPACITY+10];memset(huge,0x11,sizeof huge);huge[0]=huge[1]=0xda;
    results=0;rejections=0;rs_stream_reset(&s);feed(&s,huge,sizeof huge);
    assert(!results && rejections==1);feed(&s,frame,sizeof frame);assert(results==1);
    assert(!errors);
    // The exact production parser must reject inner CRC and device errors too.
    uint8_t plain[48],wire[104];
    // Recover plaintext using the same captured ciphertext unescaped by the stream.
    rs_stream_reset(&s);feed(&s,frame,sizeof frame);memcpy(plain,s.plain,48);
    errors=results=rejections=0;
    plain[6]=1;size_t n=packet(plain,wire,true);feed(&s,wire,n);
    assert(errors==1 && results==0 && rejections==0);
    plain[6]=0;plain[20]=1;n=packet(plain,wire,true);feed(&s,wire,n);
    assert(errors==2 && results==0);
    plain[20]=0;plain[7]^=1;n=packet(plain,wire,false);feed(&s,wire,n);
    assert(rejections==1 && results==0);
    // A CRC-valid other interface is not a measurement result.
    plain[2]=0x13;n=packet(plain,wire,true);feed(&s,wire,n);
    assert(rejections==2 && results==0);
    // Two peers cannot combine each other's fragments.
    rs_stream_t other;rs_stream_reset(&s);rs_stream_reset(&other);results=0;
    feed(&s,frame,20);feed(&other,frame+20,sizeof frame-20);assert(!results);
    feed(&s,frame+20,sizeof frame-20);assert(results==1);
    puts("8 RS stream scenarios passed");
}
