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
typedef struct {cw_dropbox_command_t command;unsigned revision;bool brew;cw_brew_record_t record;cw_trace_snapshot_t *trace;} queued_command_t;
typedef enum {UPLOAD_OK,UPLOAD_FAILED,UPLOAD_CONFLICT,UPLOAD_RESPONSE_INVALID,UPLOAD_HASH_MISMATCH} upload_result_t;
static account_t account;
// The record being written: its path, the rev Dropbox returned for our last upload and its trace.
static struct {char id[64],path[96],rev[64],created[21],trace_path[96];unsigned authorization_revision,trace_samples;bool trace_truncated;} record_file;
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
    esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h)return -1;
    esp_http_client_set_method(h,HTTP_METHOD_POST);
    if(type)esp_http_client_set_header(h,"Content-Type",type);
    if(api_arg)esp_http_client_set_header(h,"Dropbox-API-Arg",api_arg);
    if(authorized) {
        char *auth=malloc(strlen(access_token)+8);if(!auth) {esp_http_client_cleanup(h);return -1;}
        sprintf(auth,"Bearer %s",access_token);esp_http_client_set_header(h,"Authorization",auth);mbedtls_platform_zeroize(auth,strlen(auth));free(auth);
    }
    int status=-1;
    if(esp_http_client_open(h,(int)length)==ESP_OK) {
        bool sent=true;
        for(size_t offset=0;offset<length;) {int written=esp_http_client_write(h,body+offset,(int)(length-offset));if(written<=0) {sent=false;break;}offset+=(size_t)written;}
        if(sent&&esp_http_client_fetch_headers(h)>=0) {
            int used=0,read=0;
            while(used<RESPONSE_MAX-1&&(read=esp_http_client_read(h,response+used,RESPONSE_MAX-1-used))>0)used+=read;
            response[used]=0;
            if(read>=0&&esp_http_client_is_complete_data_received(h))status=esp_http_client_get_status_code(h);
        }
    }
    esp_http_client_close(h);esp_http_client_cleanup(h);return status;
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
    return current()&&status==200&&!token_response(false);
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
    cJSON *reply=cJSON_Parse(response);char hash[72],stored[96];
    bool ok=text_field(reply,"content_hash",hash,sizeof hash)&&text_field(reply,"rev",rev,rev_size)&&text_field(reply,"path_lower",stored,sizeof stored);
    cJSON_Delete(reply);
    if(!ok)return UPLOAD_RESPONSE_INVALID;
    // Our paths are ASCII; path_lower is their lower-case form.
    if(strcasecmp(stored,path))return UPLOAD_RESPONSE_INVALID;
    if(strcmp(hash,expected))return UPLOAD_HASH_MISMATCH;
    return UPLOAD_OK;
}
static const char *upload_error(upload_result_t r,const char *failed,const char *conflict,const char *invalid,const char *mismatch) {
    return r==UPLOAD_OK?NULL:r==UPLOAD_CONFLICT?conflict:r==UPLOAD_RESPONSE_INVALID?invalid:r==UPLOAD_HASH_MISMATCH?mismatch:failed;
}
static bool verify_dropbox(void) {
    const char *content="{\"schema\":1,\"application\":\"coffee-watch\",\"purpose\":\"connection-check\"}\n";char rev[64];
    return upload("/connection-check.json","\"overwrite\"",content,strlen(content),rev,sizeof rev)==UPLOAD_OK;
}
static void record_status(const cw_brew_record_t *r,cw_record_sync_t state,const char *path,const char *error) {
    cw_record_status_t status={.state=state,.revision=r->revision};
    snprintf(status.id,sizeof status.id,"%.63s",r->id);snprintf(status.path,sizeof status.path,"%s",path?path:"");snprintf(status.error,sizeof status.error,"%s",error?error:"");
    ESP_LOGI("record","id=%s revision=%u state=%u path=%s error=%s",status.id,status.revision,state,status.path,status.error);demo_record_status(&status);
}
// Uploads the brew trace CSV once, before the first draft that references it.
static const char *upload_trace(cw_brew_record_t *r,const cw_trace_snapshot_t *trace) {
    char path[96];
    int p=snprintf(path,sizeof path,"/brew-%.63s-trace.csv",r->id);
    if(p<0||(size_t)p>=sizeof path)return "trace_invalid";
    size_t bound=cw_trace_csv_bound(trace->count);
    char *csv=heap_caps_malloc(bound,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!csv)return "trace_out_of_memory";
    int n=cw_trace_csv(trace->samples,trace->count,trace->brew_ms,csv,bound);
    char rev[64];
    upload_result_t result=n<0?UPLOAD_FAILED:upload(path,"\"add\"",csv,(size_t)n,rev,sizeof rev);
    free(csv);
    if(n<0)return "trace_invalid";
    const char *error=upload_error(result,"trace_upload_failed","trace_exists","trace_response_invalid","trace_readback_failed");
    if(error)return error;
    snprintf(r->trace_path,sizeof r->trace_path,"%s",path);r->trace_samples=(unsigned)trace->count;r->trace_truncated=trace->truncated;
    snprintf(record_file.trace_path,sizeof record_file.trace_path,"%s",path);record_file.trace_samples=r->trace_samples;record_file.trace_truncated=r->trace_truncated;
    ESP_LOGI("record","trace path=%s samples=%u bytes=%d",path,r->trace_samples,n);
    return NULL;
}
static void upload_record(const cw_brew_record_t *input,const cw_trace_snapshot_t *trace) {
    cw_brew_record_t copy=*input,*r=&copy;r->trace_path[0]=0;r->trace_samples=0;r->trace_truncated=false;
    char path[96]="",created[21]="",json[1536],rev[64]="",mode[96];const char *error=NULL;
    record_status(r,CW_RECORD_SAVING,NULL,NULL);
    if(!load()) {error="authorize_first";goto finished;}
    if(!clock_ready()) {error="clock_sync_failed";goto finished;}
    if(!current()) {error="authorization_changed";goto finished;}
    if(!refresh()) {error="refresh_failed";goto finished;}
    bool exists=record_file.path[0]&&record_file.rev[0]&&!strcmp(record_file.id,r->id);
    if(exists) {
        if(record_file.authorization_revision!=active_revision) {error="authorization_changed";goto finished;}
        snprintf(path,sizeof path,"%s",record_file.path);snprintf(created,sizeof created,"%s",record_file.created);
        if(record_file.trace_path[0]) {snprintf(r->trace_path,sizeof r->trace_path,"%s",record_file.trace_path);r->trace_samples=record_file.trace_samples;r->trace_truncated=record_file.trace_truncated;}
        // Update exactly the revision this watch wrote; any other change is a conflict.
        snprintf(mode,sizeof mode,"{\".tag\":\"update\",\"update\":\"%s\"}",record_file.rev);
    } else {
        if(r->complete||r->revision!=1) {error="draft_not_found";goto finished;}
        memset(&record_file,0,sizeof record_file);snprintf(record_file.id,sizeof record_file.id,"%.63s",r->id);record_file.authorization_revision=active_revision;
        snprintf(path,sizeof path,"/brew-%.63s.json",r->id);
        // Convert the sensor-event monotonic timestamp after SNTP establishes UTC.
        struct timeval wall;gettimeofday(&wall,NULL);
        time_t captured=(time_t)(((int64_t)wall.tv_sec*1000000+wall.tv_usec-esp_timer_get_time()+r->captured_us)/1000000);
        struct tm utc;gmtime_r(&captured,&utc);strftime(created,sizeof created,"%Y-%m-%dT%H:%M:%SZ",&utc);
        snprintf(record_file.created,sizeof record_file.created,"%s",created);
        if(trace&&trace->count&&(error=upload_trace(r,trace)))goto finished;
        snprintf(mode,sizeof mode,"\"add\"");
    }
    if(cw_record_json(r,created,json,sizeof json)<0) {error="record_invalid";goto finished;}
    error=upload_error(upload(path,mode,json,strlen(json),rev,sizeof rev),exists?"record_update_failed":"record_create_failed",
                       exists?"record_changed_elsewhere":"record_exists","record_response_invalid","record_readback_failed");
    if(!error) {snprintf(record_file.path,sizeof record_file.path,"%s",path);snprintf(record_file.rev,sizeof record_file.rev,"%s",rev);}
finished:
    record_status(r,error?CW_RECORD_FAILED:CW_RECORD_SAVED,path,error);
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
        if(item.brew) {
            if(item.revision!=atomic_load(&revision))record_status(&item.record,CW_RECORD_FAILED,NULL,"authorization_changed");
            else {active_revision=item.revision;wipe_session();upload_record(&item.record,item.trace);}
            free(item.trace);item.trace=NULL;
        } else {
            active_revision=item.revision;wipe_session();
            cw_dropbox_command_t command=item.command;
            portENTER_CRITICAL(&lock);view.request=command.request;portEXIT_CRITICAL(&lock);
            if(command.op==1)begin_auth(&command);
            else if(command.op==2) {
                if(store(true)) {mbedtls_platform_zeroize(&account,sizeof account);set_state("unconfigured",NULL);}else set_state("failed","storage_error");
            } else if(command.op==3) {
                if(!load())set_state("failed","authorize_first");
                else {set_state("verifying",NULL);if(!clock_ready())set_state("failed","clock_sync_failed");else if(!refresh())set_state("failed","refresh_failed");else if(!verify_dropbox())set_state("failed","upload_check_failed");else set_state("connected",NULL);}
            }
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
    if(c->op!=2&&atomic_exchange(&occupied,true))return false;
    if(c->op==2) {if(uxQueueMessagesWaiting(commands))return false;atomic_store(&occupied,true);}
    queued_command_t item={.command=*c,.revision=c->op==3?atomic_load(&revision):atomic_fetch_add(&revision,1)+1};
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
int dropbox_setup_status(char *out,int capacity) {
    public_t v;portENTER_CRITICAL(&lock);v=view;portEXIT_CRITICAL(&lock);
    cJSON *j=cJSON_CreateObject();if(!j)return -1;
    cJSON_AddNumberToObject(j,"v",2);cJSON_AddStringToObject(j,"state",v.state);cJSON_AddStringToObject(j,"error",v.error);cJSON_AddBoolToObject(j,"saved",v.saved);cJSON_AddNumberToObject(j,"request",v.request);
    bool ok=cJSON_PrintPreallocated(j,out,capacity,false);cJSON_Delete(j);return ok?(int)strlen(out):-1;
}
