// SPDX-License-Identifier: MIT
#include "drive_setup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/platform_util.h"

#define SCOPE "https://www.googleapis.com/auth/drive.file"
#define RESPONSE_MAX 8192
// Private state belongs to one task. Public snapshots contain no credentials.
typedef struct {char id[129],secret[97],refresh[2049],folder[65],file[65];} account_t;
typedef struct {char state[24],error[40],code[16],url[81],folder[65];bool saved;uint32_t request;} public_t;
typedef struct {cw_drive_command_t command;unsigned revision;} queued_command_t;
static account_t account;
static public_t view;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t commands;
static atomic_bool occupied,have_credentials;
static atomic_uint revision;
static char response[RESPONSE_MAX],access_token[2049],device_code[2049];
static int64_t expires_us,next_poll_us;
static unsigned interval;
static uint32_t active_revision;
static bool current(void) {return active_revision==atomic_load(&revision);}
static void set_state(const char *state,const char *error) {
    if(!current())return;
    portENTER_CRITICAL(&lock);
    snprintf(view.state,sizeof view.state,"%s",state);snprintf(view.error,sizeof view.error,"%s",error?error:"");
    if(strcmp(state,"awaiting_consent")) {view.code[0]=0;view.url[0]=0;}
    snprintf(view.folder,sizeof view.folder,"%s",account.folder);view.saved=atomic_load(&have_credentials);
    portEXIT_CRITICAL(&lock);ESP_LOGI("drive","state=%s error=%s",state,error?error:"none");wifi_setup_drive_changed(state);
}
static void wipe_session(void) {mbedtls_platform_zeroize(access_token,sizeof access_token);mbedtls_platform_zeroize(device_code,sizeof device_code);mbedtls_platform_zeroize(response,sizeof response);expires_us=next_poll_us=0;}
static bool store(bool erase) {
    nvs_handle_t h;if(nvs_open("cw_drive",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t rc=erase?nvs_erase_all(h):nvs_set_blob(h,"account",&account,sizeof account);
    if(rc==ESP_OK)rc=nvs_commit(h);
    nvs_close(h);if(rc==ESP_OK)atomic_store(&have_credentials,!erase);return rc==ESP_OK;
}
static bool load(void) {
    nvs_handle_t h;size_t n=sizeof account;bool ok=false;
    if(nvs_open("cw_drive",NVS_READONLY,&h)==ESP_OK) {ok=nvs_get_blob(h,"account",&account,&n)==ESP_OK&&n==sizeof account;nvs_close(h);}
    if(!ok)memset(&account,0,sizeof account);
    account.id[128]=account.secret[96]=account.refresh[2048]=account.folder[64]=account.file[64]=0;
    bool saved=ok&&account.refresh[0];atomic_store(&have_credentials,saved);return saved;
}
static bool text_field(cJSON *json,const char *key,char *out,size_t size) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);
    if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>=size)return false;
    memcpy(out,v->valuestring,strlen(v->valuestring)+1);return true;
}
static bool file_id(const char *s) {if(!*s)return false;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return false;return true;}
static bool clock_ready(void) {
    if(time(NULL)>1735689600)return true;
    esp_netif_sntp_start();return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000))==ESP_OK&&time(NULL)>1735689600;
}
static int http(const char *url,esp_http_client_method_t method,const char *body,const char *type,bool authorized) {
    mbedtls_platform_zeroize(response,sizeof response);
    esp_http_client_config_t cfg={.url=url,.timeout_ms=15000,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=2048};
    esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h)return -1;
    esp_http_client_set_method(h,method);
    if(type)esp_http_client_set_header(h,"Content-Type",type);
    if(authorized) {
        char *auth=malloc(strlen(access_token)+8);if(!auth) {esp_http_client_cleanup(h);return -1;}
        sprintf(auth,"Bearer %s",access_token);esp_http_client_set_header(h,"Authorization",auth);mbedtls_platform_zeroize(auth,strlen(auth));free(auth);
    }
    int n=body?(int)strlen(body):0,status=-1;
    if(esp_http_client_open(h,n)==ESP_OK) {
        bool sent=true;
        for(int offset=0;offset<n;) {int written=esp_http_client_write(h,body+offset,n-offset);if(written<=0) {sent=false;break;}offset+=written;}
        if(sent&&esp_http_client_fetch_headers(h)>=0) {
            int used=0,read=0;
            while(used<RESPONSE_MAX-1&&(read=esp_http_client_read(h,response+used,RESPONSE_MAX-1-used))>0)used+=read;
            response[used]=0;
            if(read>=0&&esp_http_client_is_complete_data_received(h))status=esp_http_client_get_status_code(h);
        }
    }
    esp_http_client_close(h);esp_http_client_cleanup(h);return status;
}
// OAuth form values are percent-encoded, including refresh and device tokens.
static char *form(const char *a,const char *b,const char *c,const char *d) {
    const char *names[]={"client_id", "client_secret",c};const char *values[]={a,b,d};
    size_t cap=64+3*(strlen(a)+strlen(b)+strlen(d))+strlen(c);char *out=calloc(1,cap);if(!out)return NULL;char *p=out;
    static const char hex[]="0123456789ABCDEF";
    for(unsigned i=0;i<3;i++) {if(i==1&&!b[0])continue;if(p!=out)*p++='&';p+=sprintf(p,"%s=",names[i]);for(const unsigned char *s=(const unsigned char*)values[i];*s;s++) {
        if((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'||*s=='.'||*s=='~')*p++=*s;
        else {*p++='%';*p++=hex[*s>>4];*p++=hex[*s&15];}
    }}*p=0;return out;
}
static int post_form(char *body,const char *grant) {
    if(!body)return -1;
    size_t n=strlen(body);char *full=malloc(n+strlen(grant)+1);if(!full) {mbedtls_platform_zeroize(body,n);free(body);return -1;}
    sprintf(full,"%s%s",body,grant);mbedtls_platform_zeroize(body,n);free(body);
    int status=http("https://oauth2.googleapis.com/token",HTTP_METHOD_POST,full,"application/x-www-form-urlencoded",false);
    mbedtls_platform_zeroize(full,strlen(full));free(full);return status;
}
static bool verification_url(const char *url) {return !strcmp(url,"https://www.google.com/device")||!strcmp(url,"https://google.com/device")||!strcmp(url,"https://accounts.google.com/device");}
static bool authorized_scope(cJSON *json) {
    const cJSON *scope=cJSON_GetObjectItemCaseSensitive(json,"scope");
    if(!cJSON_IsString(scope))return false;
    const char *s=scope->valuestring;size_t n=strlen(SCOPE);
    while(*s) {while(*s==' ')s++;const char *end=strchr(s,' ');size_t len=end?(size_t)(end-s):strlen(s);if(len==n&&!memcmp(s,SCOPE,n))return true;if(!end)break;s=end+1;}return false;
}
static void delete_private_json(cJSON *j) {
    if(!j)return;
    for(cJSON *c=j->child;c;c=c->next)delete_private_json(c);
    if(j->valuestring)mbedtls_platform_zeroize(j->valuestring,strlen(j->valuestring));
}
static void free_private_json(cJSON *j) {delete_private_json(j);cJSON_Delete(j);}
static bool token_response(bool initial) {
    cJSON *json=cJSON_Parse(response);
    const cJSON *type=cJSON_GetObjectItemCaseSensitive(json,"token_type"),*ttl=cJSON_GetObjectItemCaseSensitive(json,"expires_in");
    bool ok=cJSON_IsString(type)&&!strcmp(type->valuestring,"Bearer")&&cJSON_IsNumber(ttl)&&ttl->valuedouble>0&&text_field(json,"access_token",access_token,sizeof access_token);
    if(ok)for(const unsigned char *p=(const unsigned char*)access_token;*p;p++)if(*p<=32||*p>=127) {ok=false;break;}
    if(initial)ok=ok&&authorized_scope(json)&&text_field(json,"refresh_token",account.refresh,sizeof account.refresh);
    if(json)free_private_json(json);
    mbedtls_platform_zeroize(response,sizeof response);return ok;
}
static bool refresh(void) {
    int status=post_form(form(account.id,account.secret,"refresh_token",account.refresh),"&grant_type=refresh_token");
    return current()&&status==200&&token_response(false);
}
static bool folder_metadata(void) {
    char url[256];snprintf(url,sizeof url,"https://www.googleapis.com/drive/v3/files/%s?fields=id,mimeType,trashed",account.folder);
    if(http(url,HTTP_METHOD_GET,NULL,NULL,true)!=200||!current())return false;
    cJSON *j=cJSON_Parse(response);const cJSON *mime=cJSON_GetObjectItemCaseSensitive(j,"mimeType"),*trash=cJSON_GetObjectItemCaseSensitive(j,"trashed");
    bool ok=cJSON_IsString(mime)&&!strcmp(mime->valuestring,"application/vnd.google-apps.folder")&&cJSON_IsFalse(trash);cJSON_Delete(j);return ok;
}
static bool ensure_folder(void) {
    if(account.folder[0])return file_id(account.folder)&&folder_metadata();
    // Resolve the device-marked app folder before creating it.
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char marker[32],url[512];
    snprintf(marker,sizeof marker,"cw-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(url,sizeof url,"https://www.googleapis.com/drive/v3/files?q=trashed%%3Dfalse%%20and%%20mimeType%%3D%%27application/vnd.google-apps.folder%%27%%20and%%20appProperties%%20has%%20%%7Bkey%%3D%%27coffeeWatch%%27%%20and%%20value%%3D%%27%s%%27%%7D&fields=files(id)&pageSize=2",marker);
    if(http(url,HTTP_METHOD_GET,NULL,NULL,true)!=200||!current())return false;
    cJSON *j=cJSON_Parse(response),*files=cJSON_GetObjectItemCaseSensitive(j,"files");
    if(!cJSON_IsArray(files)||cJSON_GetArraySize(files)>1) {cJSON_Delete(j);return false;}
    if(cJSON_GetArraySize(files)==1) {bool ok=text_field(cJSON_GetArrayItem(files,0),"id",account.folder,sizeof account.folder)&&file_id(account.folder);cJSON_Delete(j);return ok&&store(false);}
    cJSON_Delete(j);
    char body[256];snprintf(body,sizeof body,"{\"name\":\"Coffee Watch\",\"mimeType\":\"application/vnd.google-apps.folder\",\"appProperties\":{\"coffeeWatch\":\"%s\"}}",marker);
    if(http("https://www.googleapis.com/drive/v3/files?fields=id",HTTP_METHOD_POST,body,"application/json",true)!=200||!current())return false;
    j=cJSON_Parse(response);bool ok=text_field(j,"id",account.folder,sizeof account.folder)&&file_id(account.folder);cJSON_Delete(j);return ok&&store(false);
}
static bool verify_drive(void) {
    if(!ensure_folder())return false;
    const char *content="{\"schema\":1,\"application\":\"coffee-watch\",\"purpose\":\"connection-check\"}";
    char url[512];
    if(!account.file[0]) {
        snprintf(url,sizeof url,"https://www.googleapis.com/drive/v3/files?q=%%27%s%%27%%20in%%20parents%%20and%%20trashed%%3Dfalse%%20and%%20name%%3D%%27connection-check.json%%27&fields=files(id)&pageSize=2",account.folder);
        if(http(url,HTTP_METHOD_GET,NULL,NULL,true)!=200||!current())return false;
        cJSON *j=cJSON_Parse(response),*files=cJSON_GetObjectItemCaseSensitive(j,"files");
        if(!cJSON_IsArray(files)||cJSON_GetArraySize(files)>1) {cJSON_Delete(j);return false;}
        bool ok=!cJSON_GetArraySize(files)||text_field(cJSON_GetArrayItem(files,0),"id",account.file,sizeof account.file);cJSON_Delete(j);if(!ok)return false;
    }
    if(account.file[0]) {
        if(!file_id(account.file))return false;
        // Verify its parent before updating: no writes outside the chosen folder.
        snprintf(url,sizeof url,"https://www.googleapis.com/drive/v3/files/%s?fields=parents,trashed",account.file);
        if(http(url,HTTP_METHOD_GET,NULL,NULL,true)!=200||!current())return false;
        cJSON *j=cJSON_Parse(response),*parents=cJSON_GetObjectItemCaseSensitive(j,"parents"),*parent=cJSON_GetArrayItem(parents,0);
        bool ok=cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(j,"trashed"))&&cJSON_GetArraySize(parents)==1&&cJSON_IsString(parent)&&!strcmp(parent->valuestring,account.folder);cJSON_Delete(j);if(!ok)return false;
        snprintf(url,sizeof url,"https://www.googleapis.com/upload/drive/v3/files/%s?uploadType=media&fields=id",account.file);
        if(http(url,HTTP_METHOD_PATCH,content,"application/json",true)!=200||!current())return false;
    } else {
        char body[640];snprintf(body,sizeof body,"--coffee-watch\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n{\"name\":\"connection-check.json\",\"parents\":[\"%s\"]}\r\n--coffee-watch\r\nContent-Type: application/json\r\n\r\n%s\r\n--coffee-watch--\r\n",account.folder,content);
        if(http("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id",HTTP_METHOD_POST,body,"multipart/related; boundary=coffee-watch",true)!=200||!current())return false;
        cJSON *j=cJSON_Parse(response);bool ok=text_field(j,"id",account.file,sizeof account.file)&&file_id(account.file);cJSON_Delete(j);if(!ok||!store(false))return false;
    }
    snprintf(url,sizeof url,"https://www.googleapis.com/drive/v3/files/%s?alt=media",account.file);
    return http(url,HTTP_METHOD_GET,NULL,NULL,true)==200&&current()&&!strcmp(response,content);
}
static void finish_auth(void) {
    if(!store(false)) {set_state("failed","storage_error");return;}
    set_state("verifying",NULL);
    if(!verify_drive()) {set_state("failed","drive_verification_failed");return;}
    set_state("connected",NULL);
}
static void begin_auth(const cw_drive_command_t *c) {
    memset(&account,0,sizeof account);snprintf(account.id,sizeof account.id,"%s",c->client_id);snprintf(account.secret,sizeof account.secret,"%s",c->client_secret);
    set_state("requesting_code",NULL);
    if(!clock_ready()||!current()) {set_state("failed","clock_sync_failed");return;}
    char *body=form(account.id,"","scope",SCOPE);if(!body) {set_state("failed","out_of_memory");return;}
    int status=http("https://oauth2.googleapis.com/device/code",HTTP_METHOD_POST,body,"application/x-www-form-urlencoded",false);mbedtls_platform_zeroize(body,strlen(body));free(body);
    if(!current())return;
    if(status!=200) {set_state("failed",status<0?"google_unreachable":"device_code_rejected");return;}
    cJSON *j=cJSON_Parse(response);char code[16],url[81];
    const cJSON *ttl=cJSON_GetObjectItemCaseSensitive(j,"expires_in"),*delay=cJSON_GetObjectItemCaseSensitive(j,"interval");
    bool ok=text_field(j,"device_code",device_code,sizeof device_code)&&text_field(j,"user_code",code,sizeof code)&&text_field(j,"verification_url",url,sizeof url)&&verification_url(url)&&cJSON_IsNumber(ttl)&&ttl->valuedouble>0&&ttl->valuedouble<=3600&&cJSON_IsNumber(delay)&&delay->valuedouble>=1&&delay->valuedouble<=60;
    if(ok) {interval=(unsigned)delay->valuedouble;expires_us=esp_timer_get_time()+(int64_t)ttl->valuedouble*1000000;next_poll_us=esp_timer_get_time()+(int64_t)interval*1000000;}
    free_private_json(j);mbedtls_platform_zeroize(response,sizeof response);
    if(!ok) {set_state("failed","invalid_google_response");return;}
    portENTER_CRITICAL(&lock);snprintf(view.code,sizeof view.code,"%s",code);snprintf(view.url,sizeof view.url,"%s",url);portEXIT_CRITICAL(&lock);set_state("awaiting_consent",NULL);
}
static void poll_token(void) {
    if(esp_timer_get_time()>=expires_us) {set_state("failed","authorization_expired");expires_us=0;return;}
    int status=post_form(form(account.id,account.secret,"device_code",device_code),"&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code");
    if(!current()) {expires_us=0;return;}
    if(status==200) {expires_us=0;if(token_response(true))finish_auth();else set_state("failed","invalid_token_response");return;}
    cJSON *j=cJSON_Parse(response);const cJSON *error=cJSON_GetObjectItemCaseSensitive(j,"error");
    cw_drive_poll_action_t action=cw_drive_poll_action(status,cJSON_IsString(error)?error->valuestring:NULL);
    if(action==CW_DRIVE_POLL_WAIT||action==CW_DRIVE_POLL_SLOW) {
        if(action==CW_DRIVE_POLL_SLOW)interval+=5;
        next_poll_us=esp_timer_get_time()+(int64_t)interval*1000000;
    } else {expires_us=0;set_state("failed",status<0?"google_unreachable":action==CW_DRIVE_POLL_DENIED?"access_denied":"authorization_rejected");}
    free_private_json(j);mbedtls_platform_zeroize(response,sizeof response);
}
static void drive_task(void *arg) {
    active_revision=atomic_load(&revision);bool saved=load();set_state(saved?"saved":"unconfigured",NULL);mbedtls_platform_zeroize(&account,sizeof account);
    queued_command_t item;
    for(;;) {
        TickType_t wait=portMAX_DELAY;
        if(expires_us) {int64_t deadline=next_poll_us<expires_us?next_poll_us:expires_us;int64_t remaining=deadline-esp_timer_get_time();wait=remaining<=0?0:pdMS_TO_TICKS((remaining+999)/1000);}
        if(xQueueReceive(commands,&item,wait)==pdTRUE) {
            active_revision=item.revision;wipe_session();
            cw_drive_command_t command=item.command;
            portENTER_CRITICAL(&lock);view.request=command.request;portEXIT_CRITICAL(&lock);
            if(command.op==1)begin_auth(&command);
            else if(command.op==2) {
                if(store(true)) {mbedtls_platform_zeroize(&account,sizeof account);set_state("unconfigured",NULL);}else set_state("failed","storage_error");
            } else if(command.op==3) {
                if(!load())set_state("failed","authorize_first");
                else {set_state("verifying",NULL);if(!clock_ready())set_state("failed","clock_sync_failed");else if(!refresh())set_state("failed","refresh_failed");else if(!verify_drive())set_state("failed","drive_verification_failed");else set_state("connected",NULL);}
            }
            mbedtls_platform_zeroize(&command,sizeof command);mbedtls_platform_zeroize(&item,sizeof item);
        } else if(expires_us)poll_token();
        if(!expires_us) {wipe_session();mbedtls_platform_zeroize(&account,sizeof account);if(uxQueueMessagesWaiting(commands)==0)atomic_store(&occupied,false);}
    }
}
void drive_setup_init(void) {
    commands=xQueueCreate(2,sizeof(queued_command_t));configASSERT(commands);
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");config.start=false;ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    configASSERT(xTaskCreate(drive_task,"drive-setup",12288,NULL,3,NULL)==pdPASS);
}
bool drive_setup_submit(const cw_drive_command_t *c) {
    if(c->op!=2&&atomic_exchange(&occupied,true))return false;
    if(c->op==2) {if(uxQueueMessagesWaiting(commands))return false;atomic_store(&occupied,true);}
    queued_command_t item={.command=*c,.revision=atomic_fetch_add(&revision,1)+1};
    bool ok=xQueueSend(commands,&item,0)==pdTRUE;mbedtls_platform_zeroize(&item,sizeof item);
    if(!ok&&uxQueueMessagesWaiting(commands)==0)atomic_store(&occupied,false);
    return ok;
}
int drive_setup_status(char *out,int capacity) {
    public_t v;portENTER_CRITICAL(&lock);v=view;portEXIT_CRITICAL(&lock);
    cJSON *j=cJSON_CreateObject();if(!j)return -1;
    cJSON_AddNumberToObject(j,"v",1);cJSON_AddStringToObject(j,"state",v.state);cJSON_AddStringToObject(j,"error",v.error);cJSON_AddStringToObject(j,"user_code",v.code);cJSON_AddStringToObject(j,"verification_url",v.url);cJSON_AddStringToObject(j,"folder",v.folder);cJSON_AddBoolToObject(j,"saved",v.saved);cJSON_AddNumberToObject(j,"request",v.request);
    bool ok=cJSON_PrintPreallocated(j,out,capacity,false);cJSON_Delete(j);return ok?(int)strlen(out):-1;
}
