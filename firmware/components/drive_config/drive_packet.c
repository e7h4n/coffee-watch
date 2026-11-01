// SPDX-License-Identifier: MIT
#include "drive_packet.h"
#include <string.h>
bool cw_drive_packet_decode(const uint8_t *d,size_t n,cw_drive_command_t *out) {
    if(!d||!out||n<8||d[0]!=1)return false;
    size_t a=d[2],b=d[3];
    if(n!=8+a+b||a>CW_DRIVE_ID_MAX||b>CW_DRIVE_SECRET_MAX)return false;
    if(d[1]==1) {
        static const char suffix[]=".apps.googleusercontent.com";
        if(a<sizeof suffix||!b||memcmp(d+8+a-(sizeof suffix-1),suffix,sizeof suffix-1))return false;
        for(size_t i=8;i<n;i++)if(!((d[i]>='A'&&d[i]<='Z')||(d[i]>='a'&&d[i]<='z')||(d[i]>='0'&&d[i]<='9')||d[i]=='-'||d[i]=='_'||d[i]=='.'))return false;
    } else if((d[1]!=2&&d[1]!=3)||a||b)return false;
    uint32_t id=(uint32_t)d[4]|(uint32_t)d[5]<<8|(uint32_t)d[6]<<16|(uint32_t)d[7]<<24;
    if(!id)return false;
    memset(out,0,sizeof *out);out->op=d[1];out->request=id;
    memcpy(out->client_id,d+8,a);memcpy(out->client_secret,d+8+a,b);return true;
}

cw_drive_poll_action_t cw_drive_poll_action(int status,const char *error) {
    if(!error)return CW_DRIVE_POLL_FAIL;
    if(status==428&&!strcmp(error,"authorization_pending"))return CW_DRIVE_POLL_WAIT;
    if(status==403&&!strcmp(error,"slow_down"))return CW_DRIVE_POLL_SLOW;
    if(status==403&&!strcmp(error,"access_denied"))return CW_DRIVE_POLL_DENIED;
    return CW_DRIVE_POLL_FAIL;
}
