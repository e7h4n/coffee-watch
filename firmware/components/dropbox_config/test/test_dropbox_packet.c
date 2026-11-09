// SPDX-License-Identifier: MIT
#include "dropbox_packet.h"
#include <assert.h>
#include <string.h>
static size_t pack(uint8_t *p,uint8_t op,const char *key,const char *secret,const char *code,uint32_t id) {
    size_t a=strlen(key),b=strlen(secret),c=strlen(code);
    p[0]=2;p[1]=op;p[2]=(uint8_t)a;p[3]=(uint8_t)b;p[4]=id&255;p[5]=id>>8&255;p[6]=id>>16&255;p[7]=id>>24;p[8]=(uint8_t)c;
    memcpy(p+9,key,a);memcpy(p+9+a,secret,b);memcpy(p+9+a+b,code,c);return 9+a+b+c;
}
int main(void) {
    uint8_t p[CW_DROPBOX_PACKET_MAX+8];cw_dropbox_command_t out;
    size_t n=pack(p,1,"abc123def456ghi","0secret0secret0","Xy_9-AbCdEf123456789012345678901234567890ab",42);
    assert(cw_dropbox_packet_decode(p,n,&out));assert(out.op==1&&out.request==42);
    assert(!strcmp(out.key,"abc123def456ghi")&&!strcmp(out.secret,"0secret0secret0")&&!strcmp(out.code,"Xy_9-AbCdEf123456789012345678901234567890ab"));
    assert(!cw_dropbox_packet_decode(p,n-1,&out));
    p[0]=1;assert(!cw_dropbox_packet_decode(p,n,&out));p[0]=2;
    p[4]=0;assert(!cw_dropbox_packet_decode(p,n,&out));p[4]=42;
    p[9]='-';assert(!cw_dropbox_packet_decode(p,n,&out));p[9]='a';     // key is alphanumeric only
    p[n-1]=' ';assert(!cw_dropbox_packet_decode(p,n,&out));p[n-1]='b'; // code charset
    assert(!cw_dropbox_packet_decode(p,pack(p,1,"","s","c",1),&out));    // all three are required
    assert(!cw_dropbox_packet_decode(p,pack(p,1,"k","s","",1),&out));
    char longcode[CW_DROPBOX_CODE_MAX+2];memset(longcode,'a',sizeof longcode-1);longcode[sizeof longcode-1]=0;
    assert(!cw_dropbox_packet_decode(p,pack(p,1,"k","s",longcode,1),&out));
    assert(cw_dropbox_packet_decode(p,pack(p,2,"","","",43),&out)&&out.op==2&&!out.key[0]&&!out.code[0]);
    assert(cw_dropbox_packet_decode(p,pack(p,3,"","","",44),&out)&&out.op==3);
    assert(!cw_dropbox_packet_decode(p,pack(p,4,"","","",45),&out));
    assert(!cw_dropbox_packet_decode(p,pack(p,2,"k","","",46),&out));  // removal/check carry no credentials
    return 0;
}
