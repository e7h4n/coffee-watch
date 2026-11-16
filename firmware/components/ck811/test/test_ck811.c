// SPDX-License-Identifier: MIT
#include "ck811.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(ck811_name("LEFU-CK811",10)&&ck811_name("LFSmart Scale",13)&&ck811_name("LEFU-CK811-A1",13));
    assert(!ck811_name("LEFU-CK81",9)&&!ck811_name("BOOKOO",6)&&!ck811_name(NULL,0)&&!ck811_name("LFSmart",7));
    // 123.4 g, positive; the same magnitude with the sign byte set; display unit byte ignored.
    const uint8_t plus[9]={0xAC,0x01,0x00,0xD2,0x04,0x00,0x00,0x00,0x02};
    const uint8_t minus[9]={0xAC,0x01,0x00,0xD2,0x04,0x01,0x00,0x00,0x00};
    int32_t cg=0;
    assert(ck811_weight(plus,9,&cg)&&cg==12340);
    assert(ck811_weight(minus,9,&cg)&&cg==-12340);
    const uint8_t full[9]={0,0,0,0x50,0xC3,0,0,0,0};
    assert(ck811_weight(full,9,&cg)&&cg==500000); // 5000.0 g, the full range
    assert(!ck811_weight(plus,8,&cg)&&!ck811_weight(NULL,9,&cg));
    puts("ck811 tests passed");
    return 0;
}
