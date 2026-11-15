// SPDX-License-Identifier: MIT
#include "max30101.h"
bool max30101_identify(const max30101_bus_t *bus) {
    uint8_t id=0;
    return bus->read(bus->ctx,MAX30101_REG_PART_ID,&id,1)&&id==MAX30101_PART_ID;
}
static bool clear_fifo(const max30101_bus_t *bus) {
    return bus->write(bus->ctx,MAX30101_REG_FIFO_WR,0)&&bus->write(bus->ctx,MAX30101_REG_OVF,0)&&bus->write(bus->ctx,MAX30101_REG_FIFO_RD,0);
}
bool max30101_configure(const max30101_bus_t *bus,uint8_t ir_pa) {
    if(!bus->write(bus->ctx,MAX30101_REG_MODE,0x40))return false;
    uint8_t mode=0x40;
    for(int i=0;i<10&&(mode&0x40);i++) {
        bus->delay_ms(bus->ctx,1);
        if(!bus->read(bus->ctx,MAX30101_REG_MODE,&mode,1))return false;
    }
    if(mode&0x40)return false;
    static const uint8_t setup[][2]={
        {MAX30101_REG_FIFO_CONFIG,0x40|0x10|0x0F}, // SMP_AVE 4, rollover, almost-full unused
        {MAX30101_REG_MODE,0x03},                  // SpO2: red + IR
        {MAX30101_REG_SPO2,0x60|0x00|0x03},        // 16384 nA, 50 sps, 411 us (18 bit)
        {MAX30101_REG_LED_RED,0},{MAX30101_REG_LED_GREEN,0},{MAX30101_REG_LED_PILOT,0},
        {MAX30101_REG_SLOTS,0x20},{MAX30101_REG_SLOTS2,0}, // slot 2 = IR, as the Roast Meter sets
    };
    for(size_t i=0;i<sizeof setup/sizeof setup[0];i++)if(!bus->write(bus->ctx,setup[i][0],setup[i][1]))return false;
    return max30101_set_ir(bus,ir_pa);
}
bool max30101_set_ir(const max30101_bus_t *bus,uint8_t ir_pa) {
    return bus->write(bus->ctx,MAX30101_REG_LED_IR,ir_pa)&&clear_fifo(bus);
}
int max30101_read_ir(const max30101_bus_t *bus,uint32_t *ir,int max) {
    uint8_t pointers[3];
    if(!bus->read(bus->ctx,MAX30101_REG_FIFO_WR,pointers,3))return -1;
    uint8_t wr=pointers[0]&31,overflow=pointers[1]&31,rd=pointers[2]&31;
    int available=(wr-rd)&31;
    // Equal pointers with overflow mean a full FIFO (rollover kept the newest 32).
    if(!available&&overflow)available=MAX30101_FIFO_DEPTH;
    int n=0;
    while(n<available) {
        uint8_t data[MAX30101_SAMPLE_BYTES*4];
        int chunk=available-n;if(chunk>4)chunk=4;
        if(!bus->read(bus->ctx,MAX30101_REG_FIFO_DATA,data,(size_t)chunk*MAX30101_SAMPLE_BYTES))return -1;
        for(int i=0;i<chunk;i++) {
            const uint8_t *s=data+i*MAX30101_SAMPLE_BYTES+3;
            uint32_t value=((uint32_t)s[0]<<16|(uint32_t)s[1]<<8|s[2])&0x3FFFF;
            // Only the newest samples fit when the caller has less room.
            int skip=available-max;
            if(n+i>=skip)ir[n+i-(skip>0?skip:0)]=value;
        }
        n+=chunk;
    }
    return available<max?available:max;
}
