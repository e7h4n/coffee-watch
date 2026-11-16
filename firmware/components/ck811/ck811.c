// SPDX-License-Identifier: MIT
#include "ck811.h"
#include <string.h>
static bool prefix(const char *name,size_t length,const char *want) {size_t n=strlen(want);return length>=n&&!memcmp(name,want,n);}
bool ck811_name(const char *name,size_t length) {return name&&(prefix(name,length,"LEFU-CK811")||prefix(name,length,"LFSmart Scale"));}
bool ck811_weight(const uint8_t *f,size_t n,int32_t *cg) {
    if(!f||n<CK811_FRAME_MIN||!cg)return false;
    int32_t tenths=(int32_t)f[3]|(int32_t)f[4]<<8;
    *cg=(f[5]?-tenths:tenths)*10;
    return true;
}
