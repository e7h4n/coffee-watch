// SPDX-License-Identifier: MIT
#include "color_sensor.h"
#include "max30101.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#define SDA_GPIO GPIO_NUM_5
#define SCL_GPIO GPIO_NUM_6
// 50 samples/s averaged by 4.
#define SAMPLE_US 80000
static const char *TAG="color";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static QueueHandle_t samples;
// All sensor register traffic happens in the sensor task; Home only probes while it is idle.
static _Atomic unsigned requested_pa,errors;
static atomic_bool running,active;
static bool bus_write(void *ctx,uint8_t reg,uint8_t value) {
    (void)ctx;uint8_t b[2]={reg,value};return i2c_master_transmit(device,b,2,50)==ESP_OK;
}
static bool bus_read(void *ctx,uint8_t reg,uint8_t *data,size_t n) {
    (void)ctx;return i2c_master_transmit_receive(device,&reg,1,data,n,50)==ESP_OK;
}
static void bus_delay(void *ctx,unsigned ms) {(void)ctx;vTaskDelay(pdMS_TO_TICKS(ms?ms:1));}
static const max30101_bus_t sensor={bus_write,bus_read,bus_delay,NULL};
static void sensor_task(void *arg) {
    unsigned pa=0;
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(40));
        if(!atomic_load(&running)) {
            if(atomic_load(&active)) {max30101_shutdown(&sensor);atomic_store(&active,false);ESP_LOGI(TAG,"sensor stopped");}
            continue;
        }
        unsigned want=atomic_load(&requested_pa);
        if(!atomic_load(&active)) {
            if(!max30101_configure(&sensor,(uint8_t)want)) {atomic_fetch_add(&errors,1);continue;}
            pa=want;atomic_store(&active,true);ESP_LOGI(TAG,"sensor started, IR amplitude %u",pa);
        } else if(want!=pa) {
            if(max30101_set_ir(&sensor,(uint8_t)want)) {pa=want;ESP_LOGI(TAG,"IR amplitude %u",pa);}
            else {atomic_fetch_add(&errors,1);continue;}
        }
        uint32_t ir[MAX30101_FIFO_DEPTH];
        int n=max30101_read_ir(&sensor,ir,MAX30101_FIFO_DEPTH);
        if(n<0) {atomic_fetch_add(&errors,1);continue;}
        atomic_store(&errors,0);
        int64_t now=esp_timer_get_time();
        for(int i=0;i<n;i++) {
            color_sample_t s={.raw=ir[i],.pa=(uint8_t)pa,.us=now-(int64_t)(n-1-i)*SAMPLE_US};
            xQueueSend(samples,&s,0);
        }
    }
}
void color_sensor_init(void) {
    samples=xQueueCreate(64,sizeof(color_sample_t));configASSERT(samples);
    i2c_master_bus_config_t cfg={.i2c_port=I2C_NUM_1,.sda_io_num=SDA_GPIO,.scl_io_num=SCL_GPIO,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK) {bus=NULL;ESP_LOGE(TAG,"G5/G6 I2C unavailable; no color sensor");return;}
    i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=MAX30101_ADDRESS,.scl_speed_hz=400000};
    if(i2c_master_bus_add_device(bus,&dc,&device)!=ESP_OK) {device=NULL;ESP_LOGE(TAG,"MAX30101 device not added");return;}
    configASSERT(xTaskCreate(sensor_task,"color",4096,NULL,4,NULL)==pdPASS);
}
bool color_sensor_probe(void) {
    if(!bus||!device||atomic_load(&running))return false;
    return i2c_master_probe(bus,MAX30101_ADDRESS,20)==ESP_OK&&max30101_identify(&sensor);
}
void color_sensor_start(uint8_t ir_pa) {
    xQueueReset(samples);atomic_store(&errors,0);atomic_store(&requested_pa,ir_pa);atomic_store(&active,false);atomic_store(&running,true);
}
void color_sensor_stop(void) {atomic_store(&running,false);xQueueReset(samples);}
void color_sensor_set_ir(uint8_t ir_pa) {atomic_store(&requested_pa,ir_pa);}
bool color_sensor_next(color_sample_t *out) {return samples&&xQueueReceive(samples,out,0)==pdTRUE;}
unsigned color_sensor_errors(void) {return atomic_load(&errors);}
