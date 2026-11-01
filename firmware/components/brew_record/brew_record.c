// SPDX-License-Identifier: MIT
#include "brew_record.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
void cw_record_init(cw_record_tracker_t *t,const char *session) {memset(t,0,sizeof *t);snprintf(t->session,sizeof t->session,"%s",session);}
bool cw_record_event(cw_record_tracker_t *t,cw_stage_t before,const cw_flow_t *f,int64_t us,cw_brew_record_t *out) {
    if(before==CW_READY&&f->stage==CW_BREW) {
        memset(&t->record,0,sizeof t->record);t->sequence++;
        snprintf(t->record.id,sizeof t->record.id,"%s-%08x",t->session,t->sequence);
    }
    bool draft=before==CW_BEVERAGE&&f->stage==CW_TDS,complete=before==CW_TDS&&f->stage==CW_REPORT;
    if((complete&&!f->tds_valid)||(!draft&&!complete)||!t->record.id[0]||f->interrupted||!f->beverage_valid||!f->scale.result_valid||!f->scale.result.dose_valid||!f->scale.result.water_valid||f->scale.result.dose_cg<=0)return false;
    cw_brew_record_t *r=&t->record;
    if(!r->revision)r->captured_us=us;
    r->revision++;r->brewer=f->brewer;r->dose_cg=cw_mass_cg(f->scale.result.dose_cg);r->water_cg=cw_mass_cg(f->scale.result.water_cg);
    r->beverage_cg=cw_mass_cg(f->beverage_cg);r->empty_cg=cw_mass_cg(f->empty_cg);r->elapsed_ms=f->scale.result.elapsed_ms;r->complete=complete&&f->tds_valid;
    r->tds=r->complete?f->tds:0;r->temperature=r->complete?f->temperature:0;r->refraction=r->complete?f->refraction:0;r->extraction=r->complete?f->extraction:0;
    *out=*r;return true;
}
static bool identity(const char *s) {if(!*s)return false;for(;*s;s++)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||*s=='-'))return false;return true;}
static bool utc(const char *s) {
    if(!s||strlen(s)!=20)return false;
    for(unsigned i=0;i<20;i++) {
        char mark=i==4||i==7?'-':i==10?'T':i==13||i==16?':':i==19?'Z':0;
        if(mark?s[i]!=mark:s[i]<'0'||s[i]>'9')return false;
    }return true;
}
int cw_record_json(const cw_brew_record_t *r,const char *created,char *out,size_t cap) {
    if(!r||!out||!identity(r->id)||!utc(created)||r->brewer>=4||cw_mass_cg(r->dose_cg)<=0||cw_mass_cg(r->beverage_cg)<=0||!r->revision)return -1;
    static const char *brewers[]={"V60","Switch","AeroPress","Hoop"};
    char measured[256];
    if(r->complete) {
        if(!isfinite(r->tds)||r->tds<0||r->tds>=100||!isfinite(r->temperature)||!isfinite(r->refraction)||!isfinite(r->extraction))return -1;
        snprintf(measured,sizeof measured,"\"tds_percent\":%.2f,\"extraction_percent\":%.2f,\"sample_temperature_c\":%.1f,\"refractive_index\":%.5f",(double)r->tds,(double)r->extraction,(double)r->temperature,(double)r->refraction);
    } else snprintf(measured,sizeof measured,"\"tds_percent\":null,\"extraction_percent\":null,\"sample_temperature_c\":null,\"refractive_index\":null");
    int n=snprintf(out,cap,"{\"schema_version\":1,\"id\":\"%s\",\"revision\":%u,\"created_at\":\"%s\",\"status\":\"%s\",\"brewer\":\"%s\",\"dose_g\":%.1f,\"water_ml\":%.1f,\"beverage_g\":%.1f,\"empty_server_g\":%.1f,\"elapsed_ms\":%lu,\"brew_ratio\":%.1f,\"retention_g_per_g\":%.2f,%s}\n",
        r->id,r->revision,created,r->complete?"complete":"awaiting_tds",brewers[r->brewer],cw_mass_cg(r->dose_cg)/100.,cw_mass_cg(r->water_cg)/100.,cw_mass_cg(r->beverage_cg)/100.,cw_mass_cg(r->empty_cg)/100.,(unsigned long)r->elapsed_ms,(double)cw_mass_cg(r->water_cg)/cw_mass_cg(r->dose_cg),(double)(cw_mass_cg(r->water_cg)-cw_mass_cg(r->beverage_cg))/cw_mass_cg(r->dose_cg),measured);
    return n>=0&&(size_t)n<cap?n:-1;
}
