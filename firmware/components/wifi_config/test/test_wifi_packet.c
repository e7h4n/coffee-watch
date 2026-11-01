#include "wifi_packet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool decode(uint8_t op,const uint8_t *ssid,unsigned sn,const uint8_t *password,unsigned pn) {
    uint8_t b[104]={1,op,sn,pn,0x78,0x56,0x34,0x12};cw_wifi_packet_t out={0};
    if(sn<=32)memcpy(b+8,ssid,sn);if(pn<=64&&sn+pn<=96)memcpy(b+8+sn,password,pn);
    bool result=cw_wifi_packet_decode(b,8+sn+pn,&out);
    if(result)assert(out.op==op&&out.request_id==0x12345678);
    return result;
}
int main(void) {
    const uint8_t *name=(const uint8_t*)"Cafe",*password=(const uint8_t*)"password";
    assert(decode(1,name,4,password,8));assert(decode(1,name,4,password,0));
    assert(!decode(1,name,4,password,7));assert(!decode(1,name,0,password,8));
    uint8_t utf[]={0xe5,0x92,0x96,0xe5,0x95,0xa1};assert(decode(1,utf,6,password,8));
    assert(!decode(1,utf,2,password,8));uint8_t nul[]={'a',0,'b'};assert(!decode(1,nul,3,password,8));
    uint8_t surrogate[]={0xed,0xa0,0x80};assert(!decode(1,surrogate,3,password,8));
    uint8_t max_name[32];memset(max_name,'a',32);assert(decode(1,max_name,32,password,8));
    uint8_t psk[64];memset(psk,'a',64);assert(decode(1,name,4,psk,64));
    psk[63]='g';assert(!decode(1,name,4,psk,64));
    assert(decode(2,name,0,password,0));assert(!decode(2,name,4,password,0));assert(!decode(3,name,0,password,0));
    uint8_t full[]={1,1,1,0,1,0,0,0,'x'};cw_wifi_packet_t out={.op=88};
    for(unsigned n=0;n<sizeof full;n++)assert(!cw_wifi_packet_decode(full,n,&out)&&out.op==88);
    full[4]=0;assert(!cw_wifi_packet_decode(full,sizeof full,&out));
    puts("Wi-Fi packet validation passed");
}
