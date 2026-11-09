// SPDX-License-Identifier: MIT
#include "probe_trace.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#define NTC_GPIO 8
#define REF_GPIO 9
#define OVERSAMPLE 8
#define RAW_MAX 4095
static const char *TAG="probe";
static adc_oneshot_unit_handle_t adc;
static adc_channel_t ntc_channel,ref_channel;
static adc_cali_handle_t ntc_cali,ref_cali;
static SemaphoreHandle_t lock;
static cw_trace_t trace;
static int32_t weight_cg=CW_TRACE_NO_WEIGHT;
static probe_reading_t latest;
static adc_cali_handle_t calibration(adc_channel_t channel) {
    adc_cali_handle_t handle=NULL;
    adc_cali_curve_fitting_config_t c={.unit_id=ADC_UNIT_1,.chan=channel,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if(adc_cali_create_scheme_curve_fitting(&c,&handle)!=ESP_OK) {ESP_LOGW(TAG,"no ADC calibration for channel %d; using raw codes",channel);return NULL;}
    return handle;
}
static bool millivolts(adc_channel_t channel,adc_cali_handle_t cali,int *out,bool *saturated) {
    int raw=0;
    if(adc_oneshot_read(adc,channel,&raw)!=ESP_OK)return false;
    if(raw>=RAW_MAX)*saturated=true;
    if(!cali) {*out=raw;return true;}
    return adc_cali_raw_to_voltage(cali,raw,out)==ESP_OK;
}
// Interleaved reads keep the two channels close in time; the ratio cancels 3V3 drift.
static bool read_pair(int *ntc,int *ref,bool *saturated) {
    int sum_ntc=0,sum_ref=0;
    for(int i=0;i<OVERSAMPLE;i++) {
        int a,b;
        if(!millivolts(ntc_channel,ntc_cali,&a,saturated)||!millivolts(ref_channel,ref_cali,&b,saturated))return false;
        sum_ntc+=a;sum_ref+=b;
    }
    *ntc=sum_ntc/OVERSAMPLE;*ref=sum_ref/OVERSAMPLE;return true;
}
static void probe_task(void *arg) {
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        int ntc=0,ref=0;bool saturated=false;
        bool ok=adc&&read_pair(&ntc,&ref,&saturated);
        // A saturated G8 (open probe, or below ~25 C near the 12 dB range limit) is not a temperature.
        double ratio=ok&&ref>200?(double)ntc/ref:NAN,celsius=saturated?NAN:cw_probe_celsius(ratio);
        int64_t now=esp_timer_get_time();
        xSemaphoreTake(lock,portMAX_DELAY);
        cw_trace_append(&trace,now,weight_cg,cw_probe_cc(celsius));
        latest=(probe_reading_t){.valid=isfinite(celsius),.active=trace.active,.brew_started=trace.brew_started,.truncated=trace.truncated,
            .celsius=(float)celsius,.ratio=(float)ratio,.ntc_mv=ntc,.ref_mv=ref,.count=(unsigned)trace.count};
        xSemaphoreGive(lock);
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(CW_TRACE_PERIOD_US/1000));
    }
}
void probe_trace_init(void) {
    lock=xSemaphoreCreateMutex();configASSERT(lock);
    cw_trace_sample_t *storage=heap_caps_malloc(sizeof(cw_trace_sample_t)*CW_TRACE_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!storage)ESP_LOGE(TAG,"no memory for the brew trace; recording disabled");
    cw_trace_init(&trace,storage,CW_TRACE_CAPACITY);
    adc_unit_t ntc_unit,ref_unit;
    if(adc_oneshot_io_to_channel(NTC_GPIO,&ntc_unit,&ntc_channel)!=ESP_OK||adc_oneshot_io_to_channel(REF_GPIO,&ref_unit,&ref_channel)!=ESP_OK||ntc_unit!=ADC_UNIT_1||ref_unit!=ADC_UNIT_1) {
        ESP_LOGE(TAG,"G%d/G%d are not ADC1 inputs",NTC_GPIO,REF_GPIO);
    } else {
        adc_oneshot_unit_init_cfg_t unit={.unit_id=ADC_UNIT_1,.ulp_mode=ADC_ULP_MODE_DISABLE};
        adc_oneshot_chan_cfg_t channel={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
        if(adc_oneshot_new_unit(&unit,&adc)!=ESP_OK||adc_oneshot_config_channel(adc,ntc_channel,&channel)!=ESP_OK||adc_oneshot_config_channel(adc,ref_channel,&channel)!=ESP_OK) {
            ESP_LOGE(TAG,"ADC1 setup failed; probe temperature unavailable");adc=NULL;
        } else {ntc_cali=calibration(ntc_channel);ref_cali=calibration(ref_channel);}
    }
    configASSERT(xTaskCreate(probe_task,"probe",4096,NULL,4,NULL)==pdPASS);
}
void probe_trace_start(int64_t us) {xSemaphoreTake(lock,portMAX_DELAY);cw_trace_start(&trace,us);xSemaphoreGive(lock);ESP_LOGI(TAG,"trace started");}
void probe_trace_brew(int64_t us) {xSemaphoreTake(lock,portMAX_DELAY);cw_trace_brew(&trace,us);uint32_t ms=trace.brew_ms;xSemaphoreGive(lock);ESP_LOGI(TAG,"brew start at %lu ms",(unsigned long)ms);}
void probe_trace_stop(void) {
    xSemaphoreTake(lock,portMAX_DELAY);bool was=trace.active;cw_trace_stop(&trace);size_t n=trace.count;xSemaphoreGive(lock);
    if(was)ESP_LOGI(TAG,"trace stopped with %u samples",(unsigned)n);
}
void probe_trace_weight(bool valid,int32_t cg) {xSemaphoreTake(lock,portMAX_DELAY);weight_cg=valid?cg:CW_TRACE_NO_WEIGHT;xSemaphoreGive(lock);}
cw_trace_snapshot_t *probe_trace_snapshot(void) {
    cw_trace_snapshot_t *s=NULL;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!trace.active&&trace.brew_started&&trace.count) {
        size_t bytes=sizeof *s+trace.count*sizeof(cw_trace_sample_t);
        s=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(s) {
            s->count=trace.count;s->brew_ms=trace.brew_ms;s->truncated=trace.truncated;s->samples=(cw_trace_sample_t*)(s+1);
            memcpy(s->samples,trace.samples,trace.count*sizeof(cw_trace_sample_t));
        }
    }
    xSemaphoreGive(lock);
    return s;
}
void probe_trace_reading(probe_reading_t *out) {xSemaphoreTake(lock,portMAX_DELAY);*out=latest;xSemaphoreGive(lock);}
