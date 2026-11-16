// SPDX-License-Identifier: MIT
#include "brew_backup.h"
#include "nvs.h"
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKUP_NS "cw_brew_backup"
#define BACKUP_MAGIC UINT32_C(0x4357424b)
#define BACKUP_VERSION 1u
#define BACKUP_COUNT 8u

// Versioned local disk ABI, not a raw cw_brew_record_t: no bool representations,
// pointers, trace buffers or dependency on future additions to the live record.
// Native little-endian IEEE floats on the ESP32; incompatible size/version fails
// closed, without overwriting or erasing anything. All padding starts zeroed.
typedef struct {
    char id[64];
    int64_t utc_seconds, captured_us;
    uint32_t revision, brewer;
    int32_t dose_cg, water_cg, beverage_cg, empty_cg;
    uint32_t elapsed_ms;
    float tds, temperature, refraction, extraction;
    uint32_t scales, flags, drain_start_ms;
    int32_t peak_out_cgps;
} summary_t;
typedef struct {
    uint32_t magic, version, bytes, count;
    summary_t records[BACKUP_COUNT]; // newest distinct ID first
    uint32_t checksum;
} snapshot_t;

static uint32_t checksum(const snapshot_t *s) {
    const unsigned char *p=(const unsigned char *)s;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<offsetof(snapshot_t,checksum);i++) {
        crc^=p[i];
        for(unsigned b=0;b<8;b++)crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
static bool identity(const char *s) {
    size_t n=0;
    for(;n<64&&s[n];n++) {
        char c=s[n];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'))return false;
    }
    return n>0&&n<64;
}
static bool valid(const summary_t *r) {
    // Deliberately broad bounds: <=100 kg masses, <=7 days elapsed, UTC through
    // year 9999. Prevent corrupt summaries, overflow and pathological JSON sizes.
    return identity(r->id)&&r->revision>0&&r->brewer<4&&r->captured_us>=0&&
        r->utc_seconds>=0&&r->utc_seconds<=INT64_C(253402300799)&&
        r->dose_cg>0&&r->dose_cg<=10000000&&r->water_cg>=0&&r->water_cg<=10000000&&
        r->beverage_cg>0&&r->beverage_cg<=10000000&&r->empty_cg>=0&&r->empty_cg<=10000000&&
        r->elapsed_ms<=604800000u&&(r->scales==1||r->scales==2)&&r->flags<=3&&
        (!(r->flags&2u)||(r->scales==2&&r->drain_start_ms<=r->elapsed_ms))&&
        r->peak_out_cgps>=0&&r->peak_out_cgps<=10000000&&
        isfinite(r->tds)&&r->tds>=0&&r->tds<100&&
        isfinite(r->temperature)&&fabsf(r->temperature)<=1000000&&
        isfinite(r->refraction)&&fabsf(r->refraction)<=1000000&&
        isfinite(r->extraction)&&fabsf(r->extraction)<=1000000;
}
static const char *bank_key(uint32_t bank) {return bank?"summary1":"summary0";}

// Missing head is empty (an orphaned bank from an interrupted first save is not
// committed history). Any other corruption/read failure fails closed. Do not
// silently fall back and then overwrite evidence of an incompatible format.
static bool load(nvs_handle_t h,snapshot_t *s,uint32_t *bank) {
    esp_err_t err=nvs_get_u32(h,"head",bank);
    if(err==ESP_ERR_NVS_NOT_FOUND) {
        memset(s,0,sizeof *s);*bank=1;
        s->magic=BACKUP_MAGIC;s->version=BACKUP_VERSION;s->bytes=sizeof *s;
        return true;
    }
    if(err!=ESP_OK||*bank>1)return false;
    size_t size=sizeof *s;
    if(nvs_get_blob(h,bank_key(*bank),s,&size)!=ESP_OK||size!=sizeof *s||
       s->magic!=BACKUP_MAGIC||s->version!=BACKUP_VERSION||s->bytes!=sizeof *s||
       !s->count||s->count>BACKUP_COUNT||s->checksum!=checksum(s))return false;
    for(unsigned i=0;i<s->count;i++) {
        if(!valid(&s->records[i]))return false;
        for(unsigned j=0;j<i;j++)if(!strcmp(s->records[i].id,s->records[j].id))return false;
    }
    return true;
}
bool brew_backup_save(const cw_brew_record_t *r,int64_t utc) {
    if(!r)return false;
    summary_t next={0};
    memcpy(next.id,r->id,sizeof next.id);
    next.utc_seconds=utc;next.captured_us=r->captured_us;
    next.revision=r->revision;next.brewer=r->brewer;
    next.dose_cg=r->dose_cg;next.water_cg=r->water_cg;
    next.beverage_cg=r->beverage_cg;next.empty_cg=r->empty_cg;next.elapsed_ms=r->elapsed_ms;
    next.tds=r->tds;next.temperature=r->temperature;next.refraction=r->refraction;next.extraction=r->extraction;
    next.scales=r->scales?r->scales:1;next.flags=(r->complete?1u:0u)|(r->drain_valid?2u:0u);
    next.drain_start_ms=r->drain_start_ms;next.peak_out_cgps=r->peak_out_cgps;
    if(!valid(&next))return false;
    snapshot_t *s=calloc(1,sizeof *s);
    if(!s)return false;
    nvs_handle_t h;
    if(nvs_open(BACKUP_NS,NVS_READWRITE,&h)!=ESP_OK){free(s);return false;}
    uint32_t bank;
    bool ok=false;
    if(!load(h,s,&bank))goto done;
    unsigned i;
    for(i=0;i<s->count;i++)if(!strcmp(s->records[i].id,next.id))break;
    if(i<s->count) {
        if(next.revision<=s->records[i].revision)goto done;
        // Capture time belongs to the first revision. An unknown capture stays
        // unknown: a later completion's wall clock is not the original capture.
        next.utc_seconds=s->records[i].utc_seconds;
        next.captured_us=s->records[i].captured_us;
    } else {
        if(s->count<BACKUP_COUNT)s->count++;
        memmove(&s->records[1],&s->records[0],(s->count-1)*sizeof next);
        i=0;
    }
    s->records[i]=next;s->checksum=checksum(s);
    bank^=1u;
    // Never touch the selected bank. NVS blob replacement and head selection
    // use separate commits; torn/failed candidate writes leave the old bank.
    if(nvs_set_blob(h,bank_key(bank),s,sizeof *s)!=ESP_OK||nvs_commit(h)!=ESP_OK)goto done;
    if(nvs_set_u32(h,"head",bank)!=ESP_OK||nvs_commit(h)!=ESP_OK)goto done;
    ok=true;
done:
    nvs_close(h);free(s);return ok;
}

int brew_backup_json(unsigned index,char *out,size_t cap) {
    if(out&&cap)out[0]=0;
    if(!out||!cap||index>=BACKUP_COUNT)return -1;
    snapshot_t *s=calloc(1,sizeof *s);
    if(!s)return -1;
    nvs_handle_t h;
    if(nvs_open(BACKUP_NS,NVS_READONLY,&h)!=ESP_OK){free(s);return -1;}
    uint32_t bank;
    bool ok=load(h,s,&bank)&&index<s->count;
    nvs_close(h);
    if(!ok){free(s);return -1;}
    const summary_t *r=&s->records[index];
    static const char *brewers[]={"V60","Switch","AeroPress","Hoop"};
    char utc[24]="null",measured[256],drain[16]="null",peak[32]="null";
    if(r->utc_seconds)snprintf(utc,sizeof utc,"%" PRId64,r->utc_seconds);
    if(r->flags&1u)snprintf(measured,sizeof measured,
        "\"tds_percent\":%.2f,\"extraction_percent\":%.2f,\"sample_temperature_c\":%.1f,\"refractive_index\":%.5f",
        (double)r->tds,(double)r->extraction,(double)r->temperature,(double)r->refraction);
    else snprintf(measured,sizeof measured,"\"tds_percent\":null,\"extraction_percent\":null,\"sample_temperature_c\":null,\"refractive_index\":null");
    if(r->flags&2u)snprintf(drain,sizeof drain,"%" PRIu32,r->drain_start_ms);
    if(r->scales==2)snprintf(peak,sizeof peak,"%.2f",r->peak_out_cgps/100.);
    int n=snprintf(out,cap,
        "{\"schema_version\":1,\"source\":\"local_backup\",\"trace_samples_persisted\":false,\"probe_trace\":null,"
        "\"id\":\"%s\",\"revision\":%" PRIu32 ",\"captured_utc_seconds\":%s,\"captured_monotonic_us\":%" PRId64 ","
        "\"status\":\"%s\",\"brewer\":\"%s\",\"dose_g\":%.2f,\"water_ml\":%.2f,\"beverage_g\":%.2f,"
        "\"empty_server_g\":%.2f,\"elapsed_ms\":%" PRIu32 ",\"brew_ratio\":%.3f,\"retention_g_per_g\":%.3f,"
        "\"extraction_formula\":\"%s\",%s,\"scales\":%" PRIu32 ",\"drain_start_ms\":%s,\"peak_out_flow_gps\":%s}\n",
        r->id,r->revision,utc,r->captured_us,(r->flags&1u)?"complete":"awaiting_tds",brewers[r->brewer],
        r->dose_cg/100.,r->water_cg/100.,r->beverage_cg/100.,r->empty_cg/100.,r->elapsed_ms,
        (double)r->water_cg/r->dose_cg,(double)(r->water_cg-r->beverage_cg)/r->dose_cg,
        (r->brewer==1||r->brewer==2)?"immersion":"beverage",measured,r->scales,drain,peak);
    free(s);
    if(n<0||(size_t)n>=cap){out[0]=0;return -1;}
    return n;
}
