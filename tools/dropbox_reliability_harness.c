// Host-only fault boundaries for production.inc, extracted from dropbox_setup.c.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include "dropbox_setup.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#define RESPONSE_MAX 8192
#define HASH_BLOCK (4*1024*1024)
#define UPLOAD_URL "https://content.dropboxapi.com/2/files/upload"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static char response[RESPONSE_MAX];
static unsigned active_revision=7,callbacks,write_count,metadata_count,csv_writes,json_writes;
static bool refresh_ok=true,auth_current=true,clock_ok=true,allocation_fail;
static char last_error[64],last_log[256];
static void log_message(const char *fmt,...) {
    va_list a;va_start(a,fmt);vsnprintf(last_log,sizeof last_log,fmt,a);va_end(a);
}
#define ESP_LOGI(tag,...) log_message(__VA_ARGS__)
#define ESP_LOGW(tag,...) log_message(__VA_ARGS__)
static void *heap_caps_malloc(size_t n,unsigned caps) {(void)caps;return allocation_fail?NULL:malloc(n);}
void mbedtls_platform_zeroize(void *p,size_t n) {volatile unsigned char *v=p;while(n--)*v++=0;}
static bool current(void) {return auth_current;}
static bool load(void) {return true;}
static bool clock_ready(void) {return clock_ok;}
static bool refresh(void) {return refresh_ok;}
static int64_t esp_timer_get_time(void) {return 300000000;}
void wifi_setup_dropbox_changed(const char *state) {(void)state;}
void demo_record_status(const cw_record_status_t *s) {callbacks++;snprintf(last_error,sizeof last_error,"%s",s->error);}
static void set_state(const char *s,const char *e) {log_message("state=%s error=%s",s,e?e:"none");}
static int http(const char *,const char *,size_t,const char *,bool,const char *);
#include "production.inc"

// In-memory remote filesystem. Every write asserts add/update CAS semantics.
typedef struct {char path[96],hash[65],rev[64];char *data;bool used;} file_t;
static file_t files[16];
static unsigned rev_counter;
typedef enum {NORMAL,FAIL_JSON,LOST_JSON,LOST_JSON_NO_READBACK,LOST_CSV_NO_READBACK,CORRUPT_RESPONSE,CONFLICT_JSON} fault_t;
static fault_t fault;
static bool scope_missing,metadata_down;
static file_t *lookup(const char *path) {
    for(unsigned i=0;i<16;i++)if(files[i].used&&!strcmp(files[i].path,path))return &files[i];
    return NULL;
}
static file_t *put(const char *path,const char *data,size_t length) {
    file_t *f=lookup(path);
    if(!f)for(unsigned i=0;i<16;i++)if(!files[i].used) {f=&files[i];f->used=true;snprintf(f->path,sizeof f->path,"%s",path);break;}
    assert(f);free(f->data);f->data=malloc(length+1);assert(f->data);memcpy(f->data,data,length);f->data[length]=0;
    content_hash((const uint8_t*)data,length,f->hash);snprintf(f->rev,sizeof f->rev,"%016x",++rev_counter);return f;
}
static int describe(file_t *f) {
    snprintf(response,sizeof response,"{\"path_lower\":\"%s\",\"content_hash\":\"%s\",\"rev\":\"%s\"}",f->path,f->hash,f->rev);return 200;
}
static int http(const char *url,const char *body,size_t length,const char *type,bool authorized,const char *arg) {
    (void)type;assert(authorized);assert(record_file.brew_pending);response[0]=0;
    cJSON *j=cJSON_Parse(arg?arg:body);char path[96];assert(text_field(j,"path",path,sizeof path));
    file_t *f=lookup(path);
    if(strstr(url,"get_metadata")) {
        metadata_count++;cJSON_Delete(j);
        if(scope_missing) {strcpy(response,"{\"error\":{\".tag\":\"missing_scope\",\"required_scope\":\"files.metadata.read\"}}");return 403;}
        if(metadata_down)return -1;
        if(f)return describe(f);
        strcpy(response,"{\"error\":{\".tag\":\"path\",\"path\":{\".tag\":\"not_found\"}}}");return 409;
    }
    assert(!strcmp(url,UPLOAD_URL));write_count++;
    bool json=strstr(path,".json")!=NULL;if(json)json_writes++;else csv_writes++;
    cJSON *mode=cJSON_GetObjectItemCaseSensitive(j,"mode");
    if(cJSON_IsString(mode)) {assert(!strcmp(mode->valuestring,"add"));assert(!f);}
    else {char rev[64];assert(f&&text_field(mode,"update",rev,sizeof rev));assert(!strcmp(rev,f->rev));}
    cJSON_Delete(j);
    if(json&&fault==FAIL_JSON) {fault=NORMAL;return -1;}
    if(json&&fault==CONFLICT_JSON) {fault=NORMAL;put(path,"unrelated",9);return 409;}
    f=put(path,body,length);
    if((json&&(fault==LOST_JSON||fault==LOST_JSON_NO_READBACK))||(!json&&fault==LOST_CSV_NO_READBACK)) {
        metadata_down=fault!=LOST_JSON;fault=NORMAL;return -1;
    }
    if(fault==CORRUPT_RESPONSE) {fault=NORMAL;strcpy(response,"{}");return 200;}
    return describe(f);
}
static void reset(void) {
    clear_record();for(unsigned i=0;i<16;i++) {free(files[i].data);memset(&files[i],0,sizeof files[i]);}
    callbacks=write_count=metadata_count=csv_writes=json_writes=rev_counter=0;
    refresh_ok=auth_current=clock_ok=true;allocation_fail=scope_missing=metadata_down=diagnostic=false;fault=NORMAL;last_error[0]=0;
}
static cw_brew_record_t brew(void) {
    return (cw_brew_record_t){.id="recovery-0001",.revision=1,.captured_us=100000000,.dose_cg=1500,.water_cg=23530,.beverage_cg=20450,.elapsed_ms=179900,.scales=2};
}
static cw_trace_snapshot_t *trace_new(void) {
    cw_trace_snapshot_t *t=malloc(sizeof *t+2000*sizeof(cw_trace_sample_t));assert(t);
    *t=(cw_trace_snapshot_t){.count=2000,.brew_ms=10000,.dual=true,.samples=(cw_trace_sample_t*)(t+1)};
    for(unsigned i=0;i<2000;i++)t->samples[i]=(cw_trace_sample_t){.t_ms=i*100,.weight_cg=(int32_t)(23530*i/1999),.aux_cg=(int32_t)(20450*i/1999),.probe_cc=9200};
    return t;
}
static void complete(cw_brew_record_t *r) {r->revision=2;r->complete=true;r->tds=1.3181f;r->extraction=20.953f;}
static file_t *brew_file(void) {return lookup("/brew-recovery-0001.json");}
static void check_final(void) {
    file_t *f=brew_file();assert(f);cJSON *j=cJSON_Parse(f->data);assert(j);
    char status[32];assert(text_field(j,"status",status,sizeof status)&&!strcmp(status,"complete"));
    assert(cJSON_GetObjectItemCaseSensitive(j,"revision")->valueint>=2);
    assert(cJSON_GetObjectItemCaseSensitive(j,"water_ml")->valuedouble==235.3);
    assert(cJSON_GetObjectItemCaseSensitive(j,"beverage_g")->valuedouble==204.5);
    assert(cJSON_GetObjectItemCaseSensitive(j,"tds_percent")->valuedouble==1.32);
    assert(cJSON_GetObjectItemCaseSensitive(j,"extraction_percent")->valuedouble==20.95);
    assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j,"probe_trace"),"samples")->valueint==2000);
    cJSON_Delete(j);file_t *csv=lookup("/brew-recovery-0001-trace.csv");assert(csv&&strstr(csv->data,"out_g"));
    assert(!record_file.trace&&!record_file.brew_pending&&!retained_unsaved());
}
int main(void) {
    cw_brew_record_t r;
    // Real production upload/reconciliation: a confirmed draft keeps the trace
    // for future TDS, but Home abandoning that flow must not prevent idle OTA.
    reset();r=brew();
    assert(!upload_record(&r,trace_new()));
    assert(record_file.trace&&record_file.json_hash[0]&&record_file.trace_hash[0]);
    assert(!record_file.pending_hash[0]&&!record_file.trace_pending_hash[0]);
    assert(!record_file.brew_pending&&!retained_unsaved());
    // Rejected records must not mark the unrelated cached draft unconfirmed.
    __typeof__(record_file) confirmed=record_file;
    cw_brew_record_t wrong=r;strcpy(wrong.id,"wrong-id");wrong.revision=2;
    assert(!strcmp(upload_record(&wrong,trace_new()),"draft_not_found"));
    wrong.complete=true;assert(!strcmp(upload_record(&wrong,NULL),"draft_not_found"));
    assert(!memcmp(&confirmed,&record_file,sizeof confirmed));
    active_revision++;
    assert(!strcmp(upload_record(&r,trace_new()),"authorization_changed"));
    active_revision--;auth_current=false;
    wrong.revision=1;wrong.complete=false;
    assert(!strcmp(upload_record(&wrong,trace_new()),"authorization_changed"));
    auth_current=true;assert(!memcmp(&confirmed,&record_file,sizeof confirmed));

    // New resumed revision fails before network/hash updates despite confirmed
    // old hashes: it remains unconfirmed until CSV AND its new JSON succeed.
    r.revision=2;r.elapsed_ms=199900;refresh_ok=false;
    cw_trace_snapshot_t *resumed=trace_new();resumed->samples[1999].weight_cg=23700;
    assert(!strcmp(upload_record(&r,resumed),"refresh_failed"));
    assert(record_file.trace==resumed&&record_file.brew_pending&&retained_unsaved());
    assert(!record_file.pending_hash[0]&&!record_file.trace_pending_hash[0]);
    __typeof__(record_file) unconfirmed=record_file;
    wrong.revision=2;
    assert(!strcmp(upload_record(&wrong,NULL),"draft_not_found"));
    assert(!memcmp(&unconfirmed,&record_file,sizeof unconfirmed));
    refresh_ok=true;scope_missing=true;
    assert(!strcmp(upload_record(&r,NULL),"metadata_scope_required"));
    assert(record_file.brew_pending&&retained_unsaved());scope_missing=false;
    fault=FAIL_JSON;assert(!strcmp(upload_record(&r,NULL),"remote_content_conflict"));
    assert(csv_writes==2&&json_writes==2&&record_file.brew_pending&&retained_unsaved());
    assert(!upload_record(&r,NULL));
    assert(record_file.trace&&!record_file.brew_pending&&!retained_unsaved());
    assert(!record_file.pending_hash[0]&&!record_file.trace_pending_hash[0]);
    // A new complete attempt must revoke prior draft confirmation even without
    // a new snapshot, network access or pending hashes.
    complete(&r);r.revision=3;clock_ok=false;
    assert(!strcmp(upload_record(&r,NULL),"clock_sync_failed"));
    assert(record_file.brew_pending&&retained_unsaved());
    assert(!record_file.pending_hash[0]&&!record_file.trace_pending_hash[0]);
    clock_ok=true;assert(!upload_record(&r,NULL));check_final();
    puts("PASS confirmed cached draft allows idle OTA; resumed/complete failures block until confirmed");

    // Even the first clock failure with no trace/hash must block maintenance.
    reset();r=brew();clock_ok=false;
    assert(!strcmp(upload_record(&r,NULL),"clock_sync_failed"));
    assert(!record_file.trace&&!record_file.pending_hash[0]&&!record_file.trace_pending_hash[0]);
    assert(record_file.brew_pending&&retained_unsaved()&&!write_count);
    clock_ok=true;assert(!upload_record(&r,NULL));assert(!retained_unsaved());
    puts("PASS first clock failure without snapshot or pending hashes remains unsaved");

    reset();r=brew();refresh_ok=false;cw_trace_snapshot_t *t=trace_new();
    assert(!strcmp(upload_record(&r,t),"refresh_failed"));assert(record_file.trace==t&&write_count==0&&record_file.brew_pending&&retained_unsaved());
    refresh_ok=true;complete(&r);assert(!upload_record(&r,NULL));check_final();assert(csv_writes==1&&json_writes==1);
    puts("PASS first refresh failure -> complete create with retained 2000-sample dual-scale trace");

    reset();r=brew();fault=FAIL_JSON;assert(!strcmp(upload_record(&r,trace_new()),"upload_unconfirmed"));
    assert(record_file.trace&&csv_writes==1&&!brew_file()&&record_file.brew_pending&&retained_unsaved());complete(&r);assert(!upload_record(&r,NULL));check_final();assert(csv_writes==1&&json_writes==2);
    puts("PASS CSV success / JSON absent failure -> verified complete recovery, no CSV replay");

    reset();r=brew();fault=LOST_JSON_NO_READBACK;assert(!strcmp(upload_record(&r,trace_new()),"remote_state_unknown"));
    assert(brew_file()&&record_file.pending_hash[0]&&json_writes==1&&record_file.brew_pending&&retained_unsaved());metadata_down=false;complete(&r);
    assert(!upload_record(&r,NULL));check_final();assert(json_writes==2);
    puts("PASS ambiguous committed draft -> hash/rev reconciliation -> exact final update");

    reset();r=brew();fault=LOST_JSON;assert(!upload_record(&r,trace_new()));assert(json_writes==1);
    complete(&r);assert(!upload_record(&r,NULL));check_final();
    puts("PASS lost upload response verified by readback without write replay; final JSON updated");

    reset();r=brew();fault=CORRUPT_RESPONSE;assert(!upload_record(&r,trace_new()));
    complete(&r);assert(!upload_record(&r,NULL));check_final();
    puts("PASS invalid successful response requires metadata verification");

    reset();r=brew();fault=CONFLICT_JSON;assert(!strcmp(upload_record(&r,trace_new()),"remote_content_conflict"));
    unsigned writes=write_count;complete(&r);assert(!strcmp(upload_record(&r,NULL),"remote_content_conflict"));assert(write_count==writes&&!strcmp(brew_file()->data,"unrelated"));
    puts("PASS conflicting content at create and complete refuses overwrite/replay");

    reset();r=brew();fault=LOST_JSON_NO_READBACK;assert(upload_record(&r,trace_new()));
    put("/brew-recovery-0001.json","other writer",12);metadata_down=false;writes=write_count;complete(&r);
    assert(!strcmp(upload_record(&r,NULL),"remote_content_conflict")&&write_count==writes);
    puts("PASS unknown write subsequently replaced by unrelated content is refused");

    reset();r=brew();assert(!upload_record(&r,trace_new()));file_t *f=brew_file();strcpy(f->rev,"abcdef");writes=write_count;complete(&r);
    assert(!strcmp(upload_record(&r,NULL),"remote_content_conflict")&&write_count==writes);
    puts("PASS same prior content but externally changed rev refuses update");

    reset();r=brew();scope_missing=true;assert(!strcmp(upload_record(&r,trace_new()),"metadata_scope_required"));assert(!write_count);
    scope_missing=false;complete(&r);assert(!upload_record(&r,NULL));check_final();
    reset();r=brew();metadata_down=true;assert(!strcmp(upload_record(&r,trace_new()),"remote_state_unknown"));assert(!write_count);
    puts("PASS missing metadata scope and unknown metadata prevent all writes");

    reset();r=brew();assert(!upload_record(&r,trace_new()));
    char old_hash[65];strcpy(old_hash,record_file.trace_hash);r.revision=2;r.elapsed_ms=199900;
    t=trace_new();t->samples[1999].weight_cg=23700;t->samples[1999].aux_cg=21000;
    fault=LOST_CSV_NO_READBACK;assert(!strcmp(upload_record(&r,t),"remote_state_unknown"));
    assert(record_file.trace==t&&record_file.trace_revision==2&&record_file.trace_pending_hash[0]);
    assert(record_file.brew_pending&&retained_unsaved());
    assert(csv_writes==2&&json_writes==1);metadata_down=false;complete(&r);r.revision=3;
    assert(!upload_record(&r,NULL));check_final();assert(csv_writes==2&&json_writes==2);
    assert(strcmp(old_hash,record_file.trace_hash));assert(strstr(lookup("/brew-recovery-0001-trace.csv")->data,"237.0"));
    assert(strstr(brew_file()->data,"\"revision\":3")&&strstr(brew_file()->data,"\"elapsed_ms\":199900"));
    puts("PASS resumed draft replaces snapshot; ambiguous CSV update reconciles before final, no replay");

    reset();r=brew();assert(!upload_record(&r,trace_new()));
    put("/brew-recovery-0001-trace.csv","unrelated CSV",13);writes=write_count;r.revision=2;
    t=trace_new();t->samples[1999].aux_cg=21000;
    assert(!strcmp(upload_record(&r,t),"remote_content_conflict")&&write_count==writes);
    puts("PASS resumed CSV refuses to overwrite unrelated remote content");

    reset();r=brew();complete(&r);assert(!strcmp(upload_record(&r,NULL),"draft_not_found"));assert(!write_count);
    puts("PASS no volatile draft evidence -> complete safely rejected");

    reset();r=brew();refresh_ok=false;assert(upload_record(&r,trace_new()));
    __typeof__(record_file) saved=record_file;unsigned calls=callbacks;refresh_ok=true;run_diagnostic();
    assert(!memcmp(&saved,&record_file,sizeof saved)&&callbacks==calls);
    unsigned diagnostic_files=0;for(unsigned i=0;i<16;i++)if(files[i].used) {
        assert(!strncmp(files[i].path,"/connection-check-",18));diagnostic_files++;
        if(strstr(files[i].path,".json"))assert(strstr(files[i].data,"\"diagnostic\":true")&&strstr(files[i].data,"\"status\":\"complete\""));
        else assert(strstr(files[i].data,"\n-20000,")&&strstr(files[i].data,"\n179900,"));
    }
    assert(diagnostic_files==2&&csv_writes==1&&json_writes==2);
    refresh_ok=false;run_diagnostic();assert(!memcmp(&saved,&record_file,sizeof saved)&&callbacks==calls);
    allocation_fail=true;run_diagnostic();assert(!memcmp(&saved,&record_file,sizeof saved)&&callbacks==calls);allocation_fail=false;
    puts("PASS full synthetic diagnostic, refresh failure and allocation failure restore live recovery slot and suppress UI callbacks");
    reset();r=brew();assert(!upload_record(&r,trace_new()));
    saved=record_file;calls=callbacks;run_diagnostic();
    assert(!memcmp(&saved,&record_file,sizeof saved)&&callbacks==calls);
    assert(record_file.trace&&!record_file.brew_pending&&!retained_unsaved());
    puts("PASS diagnostic also preserves confirmed cached draft and its cleared pending flag");
    reset();puts("all Dropbox reliability tests passed");return 0;
}
