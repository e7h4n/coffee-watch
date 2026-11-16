// SPDX-License-Identifier: MIT
#include "dropbox_setup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#define TOKEN_URL "https://api.dropboxapi.com/oauth2/token"
#define UPLOAD_URL "https://content.dropboxapi.com/2/files/upload"
#define WRITE_SCOPE "files.content.write"
#define RESPONSE_MAX 8192
#define HASH_BLOCK (4*1024*1024)
// Private state belongs to one task. Public snapshots contain no credentials.
typedef struct {char key[CW_DROPBOX_KEY_MAX+1],secret[CW_DROPBOX_SECRET_MAX+1],refresh[512];} account_t;
typedef struct {char state[24],error[40];bool saved;uint32_t request;} public_t;
typedef struct {cw_dropbox_command_t command;unsigned revision;bool brew,color;cw_brew_record_t record;rc_record_t color_record;cw_trace_snapshot_t *trace;} queued_command_t;
typedef enum {UPLOAD_OK,UPLOAD_FAILED,UPLOAD_CONFLICT,UPLOAD_RESPONSE_INVALID,UPLOAD_HASH_MISMATCH} upload_result_t;
static account_t account;
// The record being written: its path, the rev Dropbox returned for our last upload and its trace.
// Single volatile recovery slot, owned by this task. Reboot, authorization change,
// or a different brew discards it; there is intentionally no durable retry queue.
static struct {
    char id[64],path[96],rev[64],created[21],trace_path[96];
    char json_hash[65],pending_hash[65];
    char trace_hash[65],trace_pending_hash[65],trace_rev[64];
    unsigned authorization_revision,trace_samples,trace_revision;bool trace_truncated;
    int64_t captured_us;
    cw_trace_snapshot_t *trace;
} record_file;
static bool diagnostic;
static void clear_record(void) {free(record_file.trace);memset(&record_file,0,sizeof record_file);}
static public_t view;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t commands;
static atomic_bool occupied,have_credentials;
static atomic_uint revision;
static char response[RESPONSE_MAX],access_token[2049];
static uint32_t active_revision;
static bool current(void) {return active_revision==atomic_load(&revision);}
static void set_state(const char *state,const char *error) {
    if(!current())return;
    portENTER_CRITICAL(&lock);
    snprintf(view.state,sizeof view.state,"%s",state);snprintf(view.error,sizeof view.error,"%s",error?error:"");view.saved=atomic_load(&have_credentials);
    portEXIT_CRITICAL(&lock);ESP_LOGI("dropbox","state=%s error=%s",state,error?error:"none");wifi_setup_dropbox_changed(state);
}
static void wipe_session(void) {mbedtls_platform_zeroize(access_token,sizeof access_token);mbedtls_platform_zeroize(response,sizeof response);}
static bool store(bool erase) {
    nvs_handle_t h;if(nvs_open("cw_dropbox",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t rc=erase?nvs_erase_all(h):nvs_set_blob(h,"account",&account,sizeof account);
    if(rc==ESP_OK)rc=nvs_commit(h);
    nvs_close(h);if(rc==ESP_OK)atomic_store(&have_credentials,!erase);return rc==ESP_OK;
}
static bool load(void) {
    nvs_handle_t h;size_t n=sizeof account;bool ok=false;
    if(nvs_open("cw_dropbox",NVS_READONLY,&h)==ESP_OK) {ok=nvs_get_blob(h,"account",&account,&n)==ESP_OK&&n==sizeof account;nvs_close(h);}
    if(!ok)memset(&account,0,sizeof account);
    account.key[CW_DROPBOX_KEY_MAX]=account.secret[CW_DROPBOX_SECRET_MAX]=account.refresh[sizeof account.refresh-1]=0;
    bool saved=ok&&account.key[0]&&account.secret[0]&&account.refresh[0];atomic_store(&have_credentials,saved);return saved;
}
static bool text_field(cJSON *json,const char *key,char *out,size_t size) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);
    if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>=size)return false;
    memcpy(out,v->valuestring,strlen(v->valuestring)+1);return true;
}
static bool clock_ready(void) {
    if(time(NULL)>1735689600)return true;
    esp_netif_sntp_start();return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000))==ESP_OK&&time(NULL)>1735689600;
}
static int http(const char *url,const char *body,size_t length,const char *type,bool authorized,const char *api_arg) {
    mbedtls_platform_zeroize(response,sizeof response);
    esp_http_client_config_t cfg={.url=url,.timeout_ms=15000,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=2048,.buffer_size_tx=4096};
    const char *op=!strcmp(url,TOKEN_URL)?"token":!strcmp(url,UPLOAD_URL)?"upload":"metadata";
    esp_http_client_handle_t h=esp_http_client_init(&cfg);
    if(!h) {ESP_LOGW("dropbox","http op=%s setup=init failed request_bytes=%u sent_bytes=0",op,(unsigned)length);return -1;}
    const char *stage="method";esp_err_t setup=esp_http_client_set_method(h,HTTP_METHOD_POST);
    if(setup!=ESP_OK)goto setup_failed;
    if(type) {stage="content_type";setup=esp_http_client_set_header(h,"Content-Type",type);if(setup!=ESP_OK)goto setup_failed;}
    if(api_arg) {stage="api_arg";setup=esp_http_client_set_header(h,"Dropbox-API-Arg",api_arg);if(setup!=ESP_OK)goto setup_failed;}
    if(authorized) {
        stage="authorization";
        char *auth=malloc(strlen(access_token)+8);if(!auth) {setup=ESP_ERR_NO_MEM;goto setup_failed;}
        sprintf(auth,"Bearer %s",access_token);setup=esp_http_client_set_header(h,"Authorization",auth);mbedtls_platform_zeroize(auth,strlen(auth));free(auth);
        if(setup!=ESP_OK)goto setup_failed;
    }
    ESP_LOGI("dropbox","TLS heap internal_free=%u internal_largest=%u psram_free=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    int status=-1;size_t sent_bytes=0;int64_t started=esp_timer_get_time();
    esp_err_t opened=esp_http_client_open(h,(int)length);
    if(opened==ESP_OK) {
        bool sent=true;
        // Keep TLS records small: ESP32-S3 hardware AES needs internal DMA
        // bounce buffers even when mbedTLS itself allocates in PSRAM.
        while(sent_bytes<length) {
            size_t chunk=length-sent_bytes;if(chunk>1024)chunk=1024;
            int written=esp_http_client_write(h,body+sent_bytes,(int)chunk);
            if(written<=0||written>(int)chunk) {sent=false;break;}
            sent_bytes+=(size_t)written;
        }
        if(sent&&esp_http_client_fetch_headers(h)>=0) {
            int used=0,read=0;
            while(used<RESPONSE_MAX-1&&(read=esp_http_client_read(h,response+used,RESPONSE_MAX-1-used))>0)used+=read;
            response[used]=0;
            if(read>=0&&esp_http_client_is_complete_data_received(h))status=esp_http_client_get_status_code(h);
        }
    }
    // Log only operation/status/transport metadata, never token bodies or headers.
    ESP_LOGI("dropbox","http op=%s status=%d open=%s request_bytes=%u sent_bytes=%u response_bytes=%u elapsed_ms=%lld",op,status,esp_err_to_name(opened),(unsigned)length,(unsigned)sent_bytes,(unsigned)strlen(response),(long long)((esp_timer_get_time()-started)/1000));
    esp_http_client_close(h);esp_http_client_cleanup(h);return status;
setup_failed:
    ESP_LOGW("dropbox","http op=%s setup=%s error=%s request_bytes=%u sent_bytes=0",op,stage,esp_err_to_name(setup),(unsigned)length);
    esp_http_client_cleanup(h);return -1;
}
// application/x-www-form-urlencoded from name/value pairs (NULL-terminated); every value is percent-encoded.
static char *form(const char *name,...) {
    va_list a;size_t cap=1;
    va_start(a,name);for(const char *n=name;n;n=va_arg(a,const char*)) {const char *v=va_arg(a,const char*);cap+=strlen(n)+2+3*strlen(v);}va_end(a);
    char *out=calloc(1,cap),*p=out;if(!out)return NULL;
    static const char hex[]="0123456789ABCDEF";
    va_start(a,name);
    for(const char *n=name;n;n=va_arg(a,const char*)) {
        const char *v=va_arg(a,const char*);if(p!=out)*p++='&';p+=sprintf(p,"%s=",n);
        for(const unsigned char *s=(const unsigned char*)v;*s;s++) {
            if((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'||*s=='.'||*s=='~')*p++=*s;
            else {*p++='%';*p++=hex[*s>>4];*p++=hex[*s&15];}
        }
    }
    va_end(a);*p=0;return out;
}
static int post_form(char *body) {
    if(!body)return -1;
    int status=http(TOKEN_URL,body,strlen(body),"application/x-www-form-urlencoded",false,NULL);
    mbedtls_platform_zeroize(body,strlen(body));free(body);return status;
}
static bool has_scope(cJSON *json,const char *want) {
    const cJSON *scope=cJSON_GetObjectItemCaseSensitive(json,"scope");
    if(!cJSON_IsString(scope))return false;
    const char *s=scope->valuestring;size_t n=strlen(want);
    while(*s) {while(*s==' ')s++;const char *end=strchr(s,' ');size_t len=end?(size_t)(end-s):strlen(s);if(len==n&&!memcmp(s,want,n))return true;if(!end)break;s=end+1;}return false;
}
static void free_private_json(cJSON *j) {
    if(!j)return;
    for(cJSON *c=j->child;c;c=c->next)if(c->valuestring)mbedtls_platform_zeroize(c->valuestring,strlen(c->valuestring));
    cJSON_Delete(j);
}
// Dropbox token responses use token_type "bearer". The first exchange must
// also return a refresh token (token_access_type=offline) and the write scope.
static const char *token_response(bool initial) {
    cJSON *json=cJSON_Parse(response);
    const cJSON *type=cJSON_GetObjectItemCaseSensitive(json,"token_type"),*ttl=cJSON_GetObjectItemCaseSensitive(json,"expires_in");
    bool ok=cJSON_IsString(type)&&!strcasecmp(type->valuestring,"bearer")&&cJSON_IsNumber(ttl)&&ttl->valuedouble>0&&text_field(json,"access_token",access_token,sizeof access_token);
    if(ok)for(const unsigned char *p=(const unsigned char*)access_token;*p;p++)if(*p<=32||*p>=127) {ok=false;break;}
    const char *error=ok?NULL:"invalid_token_response";
    if(ok&&initial) {
        if(!text_field(json,"refresh_token",account.refresh,sizeof account.refresh))error="no_refresh_token";
        else if(!has_scope(json,WRITE_SCOPE))error="missing_scope";
    }
    free_private_json(json);mbedtls_platform_zeroize(response,sizeof response);return error;
}
static bool refresh(void) {
    int status=post_form(form("grant_type","refresh_token","refresh_token",account.refresh,"client_id",account.key,"client_secret",account.secret,NULL));
    if(!current()||status!=200)return false;
    const char *error=token_response(false);
    if(error)ESP_LOGW("dropbox","refresh response: %s",error);
    return !error;
}
// Dropbox content_hash: SHA-256 of the concatenated SHA-256 digests of each 4 MiB block.
static void content_hash(const uint8_t *data,size_t length,char out[65]) {
    mbedtls_sha256_context all;uint8_t digest[32];
    mbedtls_sha256_init(&all);mbedtls_sha256_starts(&all,0);
    for(size_t offset=0;offset<length;offset+=HASH_BLOCK) {
        size_t n=length-offset<HASH_BLOCK?length-offset:HASH_BLOCK;
        mbedtls_sha256(data+offset,n,digest,0);mbedtls_sha256_update(&all,digest,sizeof digest);
    }
    mbedtls_sha256_finish(&all,digest);mbedtls_sha256_free(&all);
    for(unsigned i=0;i<32;i++)sprintf(out+2*i,"%02x",digest[i]);
}
// Uploads to an app-folder path with a Dropbox WriteMode, then requires the stored
// file's content_hash to equal the local digest. rev receives the new revision.
static upload_result_t upload(const char *path,const char *mode,const char *data,size_t length,char *rev,size_t rev_size) {
    char arg[320];
    int n=snprintf(arg,sizeof arg,"{\"path\":\"%s\",\"mode\":%s,\"autorename\":false,\"mute\":true,\"strict_conflict\":true}",path,mode);
    if(n<0||(size_t)n>=sizeof arg)return UPLOAD_FAILED;
    char expected[65];content_hash((const uint8_t*)data,length,expected);
    int status=http(UPLOAD_URL,data,length,"application/octet-stream",true,arg);
    if(!current())return UPLOAD_FAILED;
    if(status==409)return UPLOAD_CONFLICT;
    if(status!=200)return UPLOAD_FAILED;
    cJSON *reply=cJSON_Parse(response);char hash[72],stored[96],returned_rev[64];
    bool ok=text_field(reply,"content_hash",hash,sizeof hash)&&text_field(reply,"rev",returned_rev,sizeof returned_rev)&&strlen(returned_rev)<rev_size&&text_field(reply,"path_lower",stored,sizeof stored);
    cJSON_Delete(reply);
    if(!ok)return UPLOAD_RESPONSE_INVALID;
    // Our paths are ASCII; path_lower is their lower-case form.
    if(strcasecmp(stored,path))return UPLOAD_RESPONSE_INVALID;
    if(strcmp(hash,expected))return UPLOAD_HASH_MISMATCH;
    // A malformed/unverified response must not corrupt our last owned revision.
    snprintf(rev,rev_size,"%s",returned_rev);return UPLOAD_OK;
}
static const char *upload_error(upload_result_t r,const char *failed,const char *conflict,const char *invalid,const char *mismatch) {
    return r==UPLOAD_OK?NULL:r==UPLOAD_CONFLICT?conflict:r==UPLOAD_RESPONSE_INVALID?invalid:r==UPLOAD_HASH_MISMATCH?mismatch:failed;
}
static bool verify_dropbox(void) {
    const char *content="{\"schema\":1,\"application\":\"coffee-watch\",\"purpose\":\"connection-check\"}\n";char rev[64];
    return upload("/connection-check.json","\"overwrite\"",content,strlen(content),rev,sizeof rev)==UPLOAD_OK;
}
static void record_status(const cw_brew_record_t *r,cw_record_sync_t state,const char *path,const char *error) {
    if(diagnostic) {ESP_LOGI("dropbox","diagnostic stage=%s state=%u error=%s",r->complete?"final":"draft",state,error?error:"none");return;}
    cw_record_status_t status={.state=state,.revision=r->revision};
    snprintf(status.id,sizeof status.id,"%.63s",r->id);snprintf(status.path,sizeof status.path,"%s",path?path:"");snprintf(status.error,sizeof status.error,"%s",error?error:"");
    ESP_LOGI("record","id=%s revision=%u state=%u path=%s error=%s",status.id,status.revision,state,status.path,status.error);demo_record_status(&status);
}
typedef struct {char hash[65],rev[64];bool found;} remote_file_t;
// No read scope is silently requested. A missing/invalid metadata response stops
// writes, including creates: absence must be positively established by Dropbox.
static const char *metadata(const char *path,remote_file_t *file) {
    char body[160];snprintf(body,sizeof body,"{\"path\":\"%s\"}",path);
    int status=http("https://api.dropboxapi.com/2/files/get_metadata",body,strlen(body),"application/json",true,NULL);
    if(!current())return "authorization_changed";
    cJSON *j=cJSON_Parse(response),*e=cJSON_GetObjectItemCaseSensitive(j,"error");
    char tag[40]="",nested[40]="",stored[96];
    text_field(e,".tag",tag,sizeof tag);
    text_field(cJSON_GetObjectItemCaseSensitive(e,"path"),".tag",nested,sizeof nested);
    const char *error="remote_state_unknown";memset(file,0,sizeof *file);
    if(!strcmp(tag,"missing_scope"))error="metadata_scope_required";
    else if(status==409&&!strcmp(tag,"path")&&!strcmp(nested,"not_found"))error=NULL;
    else if(status==200&&text_field(j,"content_hash",file->hash,sizeof file->hash)&&strlen(file->hash)==64&&
            text_field(j,"rev",file->rev,sizeof file->rev)&&text_field(j,"path_lower",stored,sizeof stored)&&!strcasecmp(stored,path)&&
            strspn(file->rev,"0123456789abcdefABCDEF")==strlen(file->rev)) {file->found=true;error=NULL;}
    cJSON_Delete(j);return error;
}
// Reconcile before every write. Only adopt byte-identical content, or update the
// exact revision whose hash we own. A failed write gets a readback, never an
// automatic write replay. pending survives an unknown response for the next call.
static const char *reliable_upload(const char *path,const char *data,size_t length,char known[65],char pending[65],char rev[64]) {
    remote_file_t remote;const char *error=metadata(path,&remote);if(error)return error;
    char target[65],mode[96];content_hash((const uint8_t*)data,length,target);
    if(remote.found) {
        if(!strcmp(remote.hash,target))goto confirmed;
        if(pending[0]&&!strcmp(remote.hash,pending)) {
            snprintf(known,65,"%s",pending);snprintf(rev,64,"%s",remote.rev);pending[0]=0;
        }
        if(!known[0]||strcmp(remote.hash,known)||strcmp(remote.rev,rev))return "remote_content_conflict";
        snprintf(mode,sizeof mode,"{\".tag\":\"update\",\"update\":\"%s\"}",remote.rev);
    } else {
        if(known[0])return "remote_content_conflict"; // previously owned file was deleted
        snprintf(mode,sizeof mode,"\"add\"");
    }
    snprintf(pending,65,"%s",target);
    if(upload(path,mode,data,length,rev,64)==UPLOAD_OK) {
        snprintf(known,65,"%s",target);pending[0]=0;return NULL;
    }
    error=metadata(path,&remote);if(error)return error;
    if(!remote.found)return "upload_unconfirmed";
    if(strcmp(remote.hash,target))return "remote_content_conflict";
confirmed:
    snprintf(known,65,"%s",target);snprintf(rev,64,"%s",remote.rev);pending[0]=0;return NULL;
}
// Publish the latest draft's trace before the JSON that references it. Resumed
// drafts may replace CSV content, but only through the same hash/rev CAS guard.
static const char *upload_trace(cw_brew_record_t *r,const cw_trace_snapshot_t *trace) {
    char path[96];
    int p=snprintf(path,sizeof path,"/%s%.63s-trace.csv",diagnostic?"":"brew-",r->id);
    if(p<0||(size_t)p>=sizeof path)return "trace_invalid";
    size_t bound=cw_trace_csv_bound(trace->count);
    char *csv=heap_caps_malloc(bound,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!csv)return "trace_out_of_memory";
    int n=cw_trace_csv(trace->samples,trace->count,trace->brew_ms,trace->dual,csv,bound);
    const char *error=n<0?"trace_invalid":reliable_upload(path,csv,(size_t)n,record_file.trace_hash,record_file.trace_pending_hash,record_file.trace_rev);
    free(csv);
    if(error) {ESP_LOGW("dropbox","%s trace failed error=%s",diagnostic?"diagnostic":"brew",error);return error;}
    snprintf(r->trace_path,sizeof r->trace_path,"%s",path);r->trace_samples=(unsigned)trace->count;r->trace_truncated=trace->truncated;
    snprintf(record_file.trace_path,sizeof record_file.trace_path,"%s",path);record_file.trace_samples=r->trace_samples;record_file.trace_truncated=r->trace_truncated;
    ESP_LOGI("record","%s trace success path=%s samples=%u bytes=%d",diagnostic?"diagnostic":"brew",path,r->trace_samples,n);
    return NULL;
}
// Takes ownership even on failure. Establish the recovery slot BEFORE refresh,
// so a complete update can create a file after a definitely pre-upload failure.
static const char *upload_record(const cw_brew_record_t *input,cw_trace_snapshot_t *trace) {
    cw_brew_record_t copy=*input,*r=&copy;r->trace_path[0]=0;r->trace_samples=0;r->trace_truncated=false;
    char path[96]="",json[1536];const char *error=NULL;
    record_status(r,CW_RECORD_SAVING,NULL,NULL);
    if(strcmp(record_file.id,r->id)) {
        if(r->complete||r->revision!=1) {error="draft_not_found";goto finished;}
        clear_record();snprintf(record_file.id,sizeof record_file.id,"%.63s",r->id);
        record_file.authorization_revision=active_revision;record_file.captured_us=r->captured_us;
        snprintf(record_file.path,sizeof record_file.path,"/%s%.63s.json",diagnostic?"":"brew-",r->id);
    }
    if(record_file.authorization_revision!=active_revision) {error="authorization_changed";goto finished;}
    if(trace&&!r->complete&&r->revision>=record_file.trace_revision) {
        free(record_file.trace);record_file.trace=trace;trace=NULL;record_file.trace_revision=r->revision;
    }
    snprintf(path,sizeof path,"%s",record_file.path);
    if(!load()) {error="authorize_first";goto finished;}
    if(!clock_ready()) {error="clock_sync_failed";goto finished;}
    if(!current()) {error="authorization_changed";goto finished;}
    if(!record_file.created[0]) {
        struct timeval wall;gettimeofday(&wall,NULL);
        time_t captured=(time_t)(((int64_t)wall.tv_sec*1000000+wall.tv_usec-esp_timer_get_time()+record_file.captured_us)/1000000);
        struct tm utc;gmtime_r(&captured,&utc);strftime(record_file.created,sizeof record_file.created,"%Y-%m-%dT%H:%M:%SZ",&utc);
    }
    if(!refresh()) {error="refresh_failed";goto finished;}
    if(record_file.trace&&record_file.trace->count) {
        if((error=upload_trace(r,record_file.trace)))goto finished;
    }
    snprintf(r->trace_path,sizeof r->trace_path,"%s",record_file.trace_path);
    r->trace_samples=record_file.trace_samples;r->trace_truncated=record_file.trace_truncated;
    if(cw_record_json(r,record_file.created,json,sizeof json)<0) {error="record_invalid";goto finished;}
    if(diagnostic) {
        char *end=strrchr(json,'}');
        const char *label=",\"diagnostic\":true,\"purpose\":\"connection-check\"}\n";
        if(!end||strlen(label)>=(size_t)(json+sizeof json-end)) {error="record_invalid";goto finished;}
        strcpy(end,label);
    }
    error=reliable_upload(path,json,strlen(json),record_file.json_hash,record_file.pending_hash,record_file.rev);
    if(!error&&r->complete) {free(record_file.trace);record_file.trace=NULL;}
finished:
    free(trace);record_status(r,error?CW_RECORD_FAILED:CW_RECORD_SAVED,path,error);return error;
}
// Same serializers and write/reconciliation path as a real brew, but no UI status
// callbacks and no live bookkeeping changes. Diagnostic files are never brews.
static void run_diagnostic(void) {
    __typeof__(record_file) saved=record_file;memset(&record_file,0,sizeof record_file);diagnostic=true;
    cw_trace_snapshot_t *trace=heap_caps_malloc(sizeof *trace+2000*sizeof(cw_trace_sample_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const char *error="trace_out_of_memory";
    if(trace) {
        *trace=(cw_trace_snapshot_t){.count=2000,.brew_ms=20000,.dual=true,.samples=(cw_trace_sample_t*)(trace+1)};
        for(unsigned i=0;i<2000;i++)trace->samples[i]=(cw_trace_sample_t){.t_ms=i*100,.weight_cg=(int32_t)(23530*i/1999),.aux_cg=(int32_t)(20450*i/1999),.probe_cc=9200};
        cw_brew_record_t r={.revision=1,.captured_us=esp_timer_get_time(),.dose_cg=1500,.water_cg=23530,.beverage_cg=20450,.elapsed_ms=179900,.scales=2};
        static unsigned sequence;
        snprintf(r.id,sizeof r.id,"connection-check-%lld-%u",(long long)esp_timer_get_time(),++sequence);
        ESP_LOGI("dropbox","diagnostic begin id=%s samples=2000 dual=true",r.id);
        error=upload_record(&r,trace); // ownership transferred
        if(!error) {r.revision=2;r.complete=true;r.tds=1.3181f;r.extraction=20.953f;error=upload_record(&r,NULL);}
    }
    ESP_LOGI("dropbox","diagnostic result=%s error=%s",error?"failed":"success",error?error:"none");
    clear_record();record_file=saved;diagnostic=false;
    set_state(error?"failed":"connected",error);
}
static void color_status(const rc_record_t *r,cw_record_sync_t state,const char *path,const char *error) {
    cw_record_status_t status={.state=state,.revision=r->revision};
    snprintf(status.id,sizeof status.id,"%.63s",r->id);snprintf(status.path,sizeof status.path,"%s",path?path:"");snprintf(status.error,sizeof status.error,"%s",error?error:"");
    ESP_LOGI("record","color id=%s revision=%u state=%u path=%s error=%s",status.id,status.revision,state,status.path,status.error);demo_record_status(&status);
}
// A roast-color group is one document per group, replaced whole after each reading.
// After this watch has written it, updates name that exact revision; before that
// (or after a failed first upload) the path, unique to this boot, is overwritten.
static void upload_color(const rc_record_t *r) {
    char path[96]="",json[1536],rev[64]="",mode[96];const char *error=NULL;
    color_status(r,CW_RECORD_SAVING,NULL,NULL);
    if(!load()) {error="authorize_first";goto finished;}
    if(!clock_ready()) {error="clock_sync_failed";goto finished;}
    if(!current()) {error="authorization_changed";goto finished;}
    if(!refresh()) {error="refresh_failed";goto finished;}
    bool exists=record_file.path[0]&&record_file.rev[0]&&!strcmp(record_file.id,r->id)&&record_file.authorization_revision==active_revision;
    if(exists) {
        snprintf(path,sizeof path,"%s",record_file.path);
        snprintf(mode,sizeof mode,"{\".tag\":\"update\",\"update\":\"%s\"}",record_file.rev);
    } else {
        clear_record();snprintf(record_file.id,sizeof record_file.id,"%.63s",r->id);record_file.authorization_revision=active_revision;
        snprintf(path,sizeof path,"/roast-color-%.63s.json",r->id);snprintf(mode,sizeof mode,"\"overwrite\"");
    }
    struct timeval wall;gettimeofday(&wall,NULL);
    int64_t offset=(int64_t)wall.tv_sec*1000000+wall.tv_usec-esp_timer_get_time();
    if(rc_record_json(r,offset,json,sizeof json)<0) {error="record_invalid";goto finished;}
    error=upload_error(upload(path,mode,json,strlen(json),rev,sizeof rev),"record_upload_failed","record_changed_elsewhere","record_response_invalid","record_readback_failed");
    if(!error) {snprintf(record_file.path,sizeof record_file.path,"%s",path);snprintf(record_file.rev,sizeof record_file.rev,"%s",rev);}
finished:
    color_status(r,error?CW_RECORD_FAILED:CW_RECORD_SAVED,path,error);
}
static void finish_auth(void) {
    if(!store(false)) {set_state("failed","storage_error");return;}
    set_state("verifying",NULL);
    if(!verify_dropbox()) {set_state("failed","upload_check_failed");return;}
    set_state("connected",NULL);
}
// Exchanges the authorization code the user copied from Dropbox (authorize URL with
// token_access_type=offline and no redirect_uri) for a refresh token.
static void begin_auth(const cw_dropbox_command_t *c) {
    memset(&account,0,sizeof account);snprintf(account.key,sizeof account.key,"%s",c->key);snprintf(account.secret,sizeof account.secret,"%s",c->secret);
    set_state("authorizing",NULL);
    if(!clock_ready()||!current()) {set_state("failed","clock_sync_failed");return;}
    int status=post_form(form("code",c->code,"grant_type","authorization_code","client_id",account.key,"client_secret",account.secret,NULL));
    if(!current())return;
    if(status!=200) {
        cJSON *j=cJSON_Parse(response);const cJSON *e=cJSON_GetObjectItemCaseSensitive(j,"error");
        const char *reason=status<0?"dropbox_unreachable":cJSON_IsString(e)&&!strcmp(e->valuestring,"invalid_grant")?"code_rejected":
                           cJSON_IsString(e)&&!strcmp(e->valuestring,"invalid_client")?"client_rejected":"authorization_rejected";
        free_private_json(j);mbedtls_platform_zeroize(response,sizeof response);set_state("failed",reason);return;
    }
    const char *error=token_response(true);
    if(error) {set_state("failed",error);return;}
    finish_auth();
}
static void dropbox_task(void *arg) {
    active_revision=atomic_load(&revision);bool saved=load();set_state(saved?"saved":"unconfigured",NULL);mbedtls_platform_zeroize(&account,sizeof account);
    queued_command_t item;
    for(;;) {
        if(xQueueReceive(commands,&item,portMAX_DELAY)!=pdTRUE)continue;
        if(item.color) {
            if(item.revision!=atomic_load(&revision))color_status(&item.color_record,CW_RECORD_FAILED,NULL,"authorization_changed");
            else {active_revision=item.revision;wipe_session();upload_color(&item.color_record);}
        } else if(item.brew) {
            if(item.revision!=atomic_load(&revision))record_status(&item.record,CW_RECORD_FAILED,NULL,"authorization_changed");
            else {active_revision=item.revision;wipe_session();upload_record(&item.record,item.trace);item.trace=NULL;}
            free(item.trace);item.trace=NULL;
        } else {
            active_revision=item.revision;wipe_session();
            cw_dropbox_command_t command=item.command;
            portENTER_CRITICAL(&lock);view.request=command.request;portEXIT_CRITICAL(&lock);
            if(command.op==1||command.op==2)clear_record();
            if(command.op==1)begin_auth(&command);
            else if(command.op==2) {
                if(store(true)) {mbedtls_platform_zeroize(&account,sizeof account);set_state("unconfigured",NULL);}else set_state("failed","storage_error");
            } else if(command.op==3) {
                if(!load())set_state("failed","authorize_first");
                else {set_state("verifying",NULL);if(!clock_ready())set_state("failed","clock_sync_failed");else if(!refresh())set_state("failed","refresh_failed");else if(!verify_dropbox())set_state("failed","upload_check_failed");else set_state("connected",NULL);}
            }
            if(command.op==4)run_diagnostic();
            mbedtls_platform_zeroize(&command,sizeof command);
        }
        mbedtls_platform_zeroize(&item,sizeof item);wipe_session();mbedtls_platform_zeroize(&account,sizeof account);
        if(uxQueueMessagesWaiting(commands)==0)atomic_store(&occupied,false);
    }
}
void dropbox_setup_init(void) {
    commands=xQueueCreate(8,sizeof(queued_command_t));configASSERT(commands);
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");config.start=false;ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    configASSERT(xTaskCreate(dropbox_task,"dropbox",12288,NULL,3,NULL)==pdPASS);
}
bool dropbox_setup_submit(const cw_dropbox_command_t *c) {
    if(c->op<1||c->op>4)return false;
    if(c->op!=2&&atomic_exchange(&occupied,true))return false;
    if(c->op==2) {if(uxQueueMessagesWaiting(commands))return false;atomic_store(&occupied,true);}
    queued_command_t item={.command=*c,.revision=(c->op==3||c->op==4)?atomic_load(&revision):atomic_fetch_add(&revision,1)+1};
    bool ok=xQueueSend(commands,&item,0)==pdTRUE;mbedtls_platform_zeroize(&item,sizeof item);
    if(!ok&&uxQueueMessagesWaiting(commands)==0)atomic_store(&occupied,false);
    return ok;
}
bool dropbox_setup_record(const cw_brew_record_t *record,cw_trace_snapshot_t *trace) {
    if(!atomic_load(&have_credentials)) {free(trace);record_status(record,CW_RECORD_FAILED,NULL,"authorize_first");return false;}
    queued_command_t item={.revision=atomic_load(&revision),.brew=true,.record=*record,.trace=trace};
    bool ok=xQueueSend(commands,&item,0)==pdTRUE;mbedtls_platform_zeroize(&item,sizeof item);
    if(!ok) {free(trace);record_status(record,CW_RECORD_FAILED,NULL,"record_queue_full");}
    return ok;
}
bool dropbox_setup_color(const rc_record_t *record) {
    if(!atomic_load(&have_credentials)) {color_status(record,CW_RECORD_FAILED,NULL,"authorize_first");return false;}
    queued_command_t item={.revision=atomic_load(&revision),.color=true,.color_record=*record};
    bool ok=xQueueSend(commands,&item,0)==pdTRUE;mbedtls_platform_zeroize(&item,sizeof item);
    if(!ok)color_status(record,CW_RECORD_FAILED,NULL,"record_queue_full");
    return ok;
}
int dropbox_setup_status(char *out,int capacity) {
    public_t v;portENTER_CRITICAL(&lock);v=view;portEXIT_CRITICAL(&lock);
    cJSON *j=cJSON_CreateObject();if(!j)return -1;
    cJSON_AddNumberToObject(j,"v",2);cJSON_AddStringToObject(j,"state",v.state);cJSON_AddStringToObject(j,"error",v.error);cJSON_AddBoolToObject(j,"saved",v.saved);cJSON_AddNumberToObject(j,"request",v.request);
    bool ok=cJSON_PrintPreallocated(j,out,capacity,false);cJSON_Delete(j);return ok?(int)strlen(out):-1;
}
