// SPDX-License-Identifier: MIT
#include "max30101.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
// Register-level model: FIFO data reads stream bytes and advance the read pointer
// once per 6-byte sample; other reads auto-increment the register address.
typedef struct {
    uint8_t regs[256];uint32_t red[32],ir[32];unsigned byte;
    bool fail,reset_sticks;int writes;uint8_t log[64][2];
} fake_t;
static bool fake_write(void *ctx,uint8_t reg,uint8_t value) {
    fake_t *f=ctx;if(f->fail)return false;
    if(f->writes<64) {f->log[f->writes][0]=reg;f->log[f->writes][1]=value;}
    f->writes++;
    f->regs[reg]=reg==MAX30101_REG_MODE&&(value&0x40)&&!f->reset_sticks?value&~0x40:value;
    if(reg==MAX30101_REG_MODE&&(value&0x40)&&f->reset_sticks)f->regs[reg]=value;
    if(reg==MAX30101_REG_FIFO_RD)f->byte=0;
    return true;
}
static bool fake_read(void *ctx,uint8_t reg,uint8_t *data,size_t n) {
    fake_t *f=ctx;if(f->fail)return false;
    for(size_t i=0;i<n;i++) {
        if(reg==MAX30101_REG_FIFO_DATA) {
            unsigned slot=f->regs[MAX30101_REG_FIFO_RD]&31,k=f->byte%6;
            uint32_t v=k<3?f->red[slot]:f->ir[slot];
            data[i]=(uint8_t)(v>>(8*(2-k%3)));
            if(++f->byte%6==0) {f->regs[MAX30101_REG_FIFO_RD]=(slot+1)&31;f->regs[MAX30101_REG_OVF]=0;}
        } else data[i]=f->regs[(uint8_t)(reg+i)];
    }
    return true;
}
static void fake_delay(void *ctx,unsigned ms) {(void)ctx;(void)ms;}
static int scenarios;
int main(void) {
    fake_t f;memset(&f,0,sizeof f);
    max30101_bus_t bus={fake_write,fake_read,fake_delay,&f};
    f.regs[MAX30101_REG_PART_ID]=0x15;assert(max30101_identify(&bus));
    f.regs[MAX30101_REG_PART_ID]=0x11;assert(!max30101_identify(&bus));
    f.fail=true;f.regs[MAX30101_REG_PART_ID]=0x15;assert(!max30101_identify(&bus));f.fail=false;
    scenarios++;
    // Roast Meter configuration, then IR only, and an empty FIFO.
    f.regs[MAX30101_REG_FIFO_WR]=9;f.regs[MAX30101_REG_FIFO_RD]=3;
    assert(max30101_configure(&bus,135));
    assert(f.regs[MAX30101_REG_FIFO_CONFIG]==0x5F&&f.regs[MAX30101_REG_MODE]==0x03&&f.regs[MAX30101_REG_SPO2]==0x63);
    assert(f.regs[MAX30101_REG_LED_RED]==0&&f.regs[MAX30101_REG_LED_GREEN]==0&&f.regs[MAX30101_REG_LED_PILOT]==0);
    assert(f.regs[MAX30101_REG_LED_IR]==135&&f.regs[MAX30101_REG_SLOTS]==0x20);
    assert(f.log[0][0]==MAX30101_REG_MODE&&f.log[0][1]==0x40);
    assert(!f.regs[MAX30101_REG_FIFO_WR]&&!f.regs[MAX30101_REG_FIFO_RD]&&!f.regs[MAX30101_REG_OVF]);
    uint32_t ir[32];assert(max30101_read_ir(&bus,ir,32)==0);
    f.reset_sticks=true;assert(!max30101_configure(&bus,135));f.reset_sticks=false;
    scenarios++;
    // Samples wrap past slot 31; IR is the second triple, masked to 18 bits.
    for(int i=0;i<32;i++) {f.red[i]=0x3FFFF;f.ir[i]=0xFC0000u|(uint32_t)(1000+i);}
    f.regs[MAX30101_REG_FIFO_RD]=30;f.regs[MAX30101_REG_FIFO_WR]=3;
    int n=max30101_read_ir(&bus,ir,32);
    assert(n==5&&ir[0]==1030&&ir[1]==1031&&ir[2]==1000&&ir[4]==1002);
    assert(f.regs[MAX30101_REG_FIFO_RD]==3&&max30101_read_ir(&bus,ir,32)==0);
    scenarios++;
    // A full FIFO (equal pointers plus overflow) holds 32 samples; with room for 4
    // only the newest are returned and the FIFO is still drained.
    f.regs[MAX30101_REG_FIFO_RD]=7;f.regs[MAX30101_REG_FIFO_WR]=7;f.regs[MAX30101_REG_OVF]=5;
    n=max30101_read_ir(&bus,ir,4);
    assert(n==4&&ir[0]==1003&&ir[3]==1006&&f.regs[MAX30101_REG_FIFO_RD]==7);
    f.fail=true;assert(max30101_read_ir(&bus,ir,4)==-1);f.fail=false;
    scenarios++;
    // A new amplitude clears the FIFO, so no older sample is read with it.
    f.regs[MAX30101_REG_FIFO_WR]=12;assert(max30101_set_ir(&bus,200)&&f.regs[MAX30101_REG_LED_IR]==200);
    assert(max30101_read_ir(&bus,ir,32)==0);
    scenarios++;
    printf("%d max30101 scenarios passed\n",scenarios);
    return 0;
}
