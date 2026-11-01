// SPDX-License-Identifier: MIT
#include "drive_packet.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const char *id="123-test.apps.googleusercontent.com",*secret="test-secret";
    uint8_t p[CW_DRIVE_PACKET_MAX]={1,1,0,0,42};p[2]=strlen(id);p[3]=strlen(secret);
    memcpy(p+8,id,p[2]);memcpy(p+8+p[2],secret,p[3]);size_t n=8+p[2]+p[3];cw_drive_command_t out;
    assert(cw_drive_packet_decode(p,n,&out));assert(!strcmp(out.client_id,id));assert(!strcmp(out.client_secret,secret));assert(out.request==42);
    assert(!cw_drive_packet_decode(p,n-1,&out));p[4]=0;assert(!cw_drive_packet_decode(p,n,&out));p[4]=42;
    p[8]=' ';assert(!cw_drive_packet_decode(p,n,&out));p[8]='1';p[n-1]=0;assert(!cw_drive_packet_decode(p,n,&out));
    memset(p,0,sizeof p);p[0]=1;p[1]=2;p[4]=43;assert(cw_drive_packet_decode(p,8,&out));assert(!out.client_secret[0]);
    p[1]=3;assert(cw_drive_packet_decode(p,8,&out));p[1]=4;assert(!cw_drive_packet_decode(p,8,&out));
    p[1]=2;p[2]=1;assert(!cw_drive_packet_decode(p,9,&out));
    assert(cw_drive_poll_action(428,"authorization_pending")==CW_DRIVE_POLL_WAIT);
    assert(cw_drive_poll_action(403,"slow_down")==CW_DRIVE_POLL_SLOW);
    assert(cw_drive_poll_action(403,"access_denied")==CW_DRIVE_POLL_DENIED);
    assert(cw_drive_poll_action(500,"authorization_pending")==CW_DRIVE_POLL_FAIL);
    assert(cw_drive_poll_action(401,"invalid_client")==CW_DRIVE_POLL_FAIL);
    assert(cw_drive_poll_action(-1,NULL)==CW_DRIVE_POLL_FAIL);
    return 0;
}
