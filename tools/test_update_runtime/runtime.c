/* Host-only tests compile the real firmware source, not a reimplementation. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock_idf.h"
#include "esp_heap_caps.h"
#include <time.h>
static time_t mock_time(time_t *out);
#define time mock_time
static void tracked_free(void *);
/* All SUT-owned allocations must be capability-specific, including future ones.
 * Parser/OpenSSL allocations are deliberately outside this instrumentation. */
#define malloc OTA_plain_malloc_is_forbidden
#define calloc OTA_plain_calloc_is_forbidden
#define free tracked_free
#include "../../firmware/demo/main/firmware_update.c"
#undef malloc
#undef calloc
#undef free
#undef time

static struct {void *ptr; size_t size;} allocations[16];
static size_t live_bytes, peak_bytes;
static unsigned live_allocations, alloc_calls, fail_alloc;
static bool psram_available = true;
static int http_live;
size_t heap_caps_get_total_size(unsigned caps) {
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 return psram_available ? 8u*1024*1024 : 0;
}
void *heap_caps_malloc(size_t n,unsigned caps) {
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 alloc_calls++;
 if(!psram_available || alloc_calls==fail_alloc)return NULL;
 void *p=malloc(n);assert(p);
 for(size_t i=0;i<16;i++)if(!allocations[i].ptr){
  allocations[i].ptr=p;allocations[i].size=n;live_allocations++;live_bytes+=n;
  if(live_bytes>peak_bytes)peak_bytes=live_bytes;
  return p;
 }
 assert(0);return NULL;
}
void *heap_caps_calloc(size_t count,size_t n,unsigned caps) {
 assert(count==1);void *p=heap_caps_malloc(count*n,caps);
 if(p)memset(p,0,count*n);
 return p;
}
static void tracked_free(void *p) {
 if(!p)return;
 for(size_t i=0;i<16;i++)if(allocations[i].ptr==p){
  live_bytes-=allocations[i].size;live_allocations--;allocations[i].ptr=NULL;free(p);return;
 }
 assert(!"untracked allocation or double free");
}

_Static_assert(sizeof(esp_image_header_t)==24,"image header ABI");
_Static_assert(sizeof(esp_app_desc_t)==256,"app descriptor ABI");
static esp_partition_t parts[6];
static size_t part_count;
struct mock_iterator {size_t index;};
static struct mock_iterator iterator;
static esp_app_desc_t running_desc;
static esp_ota_img_states_t running_state;
static int state_error, boot_wrong;
static uint32_t flash_size;
static unsigned char image[131195], flash_bytes[131195];
static unsigned yields;
static size_t image_size, flash_used;
static int begins, writes, ends, aborts, selects, marks, cleanups, reads, restarts;
static int begin_error, end_error, write_error, partition_read_error, select_error;
static int cancel_read, cancel_begin, notify_count, expire_read;
static bool worker_manifest;
static char manifest_json[1024];
static jmp_buf worker_exit, restart_exit;
static int64_t now;
static time_t wall_time;
static unsigned sntp_starts, sntp_waits, http_calls, sync_at, cancel_wait;
static esp_err_t start_result, wait_result;
static bool expire_clock, frozen_clock;
static time_t mock_time(time_t *out) {if(out)*out=wall_time;return wall_time;}
esp_err_t esp_netif_sntp_start(void) {sntp_starts++;assert(!http_calls);return start_result;}
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks) {
 assert(ticks>0&&ticks<=1000);assert(sntp_starts==1&&!http_calls);
 sntp_waits++;assert(sntp_waits<=15);
 if(!frozen_clock)now+=(int64_t)ticks*1000;
 if(sntp_waits==sync_at)wall_time=1735689601;
 if(sntp_waits==cancel_wait)firmware_update_cancel();
 if(expire_clock)now=40000000;
 return wait_result;
}
static esp_log_level_t log_level=5;
esp_log_level_t esp_log_level_get(const char *tag) {assert(!strcmp(tag,"HTTP_CLIENT"));return log_level;}
void esp_log_level_set(const char *tag,esp_log_level_t level) {assert(!strcmp(tag,"HTTP_CLIENT"));log_level=level;}
struct mock_http {esp_http_client_config_t cfg; size_t pos;};
static struct mock_http http;
static struct {
 const unsigned char *body; size_t size, short_read;
 const char *length, *encoding, *transfer, *location;
 bool duplicate_length, duplicate_location, chunked, incomplete;
 int status; int64_t fetched; int fail_read;
} response;
static char length_text[32];

SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned t) {(void)m;(void)t;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m) {(void)m;return 1;}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned stack,void *arg,unsigned pri,TaskHandle_t *out) {(void)fn;(void)n;assert(stack==8192);(void)arg;(void)pri;*out=(void *)1;return pdPASS;}
unsigned ulTaskNotifyTake(int clear,unsigned timeout) {(void)clear;(void)timeout;if(notify_count++) longjmp(worker_exit,1);return 1;}
void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
void vTaskDelay(unsigned ticks) {assert(ticks==1);yields++;}
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label) {(void)label;for(size_t i=0;i<part_count;i++)if(parts[i].type==type&&parts[i].subtype==sub)return &parts[i];return NULL;}
esp_partition_iterator_t esp_partition_find(int t,int s,const char *l) {(void)t;(void)s;(void)l;iterator.index=0;return part_count?&iterator:NULL;}
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it) {return &parts[it->index];}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it) {return ++it->index<part_count?it:NULL;}
void esp_partition_iterator_release(esp_partition_iterator_t it) {(void)it;}
esp_err_t esp_partition_read(const esp_partition_t *p,uint32_t off,void *buf,size_t n) {assert(p==&parts[1]);assert(off+n<=flash_used);if(partition_read_error)return ESP_FAIL;memcpy(buf,flash_bytes+off,n);return ESP_OK;}
const esp_partition_t *esp_ota_get_running_partition(void) {return &parts[0];}
const esp_partition_t *esp_ota_get_boot_partition(void) {return &parts[boot_wrong?1:0];}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *s) {assert(p==&parts[0]);*s=running_state;return state_error?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_begin(const esp_partition_t *p,size_t n,esp_ota_handle_t *o) {assert(p==&parts[1]);assert(n==image_size);begins++;flash_used=0;*o=42;if(cancel_begin)firmware_update_cancel();return begin_error?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_write(esp_ota_handle_t o,const void *buf,size_t n) {assert(o==42);writes++;if(write_error)return ESP_FAIL;assert(flash_used+n<=sizeof(flash_bytes));memcpy(flash_bytes+flash_used,buf,n);flash_used+=n;return ESP_OK;}
esp_err_t esp_ota_end(esp_ota_handle_t o) {assert(o==42);ends++;return end_error?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_abort(esp_ota_handle_t o) {assert(o==42);aborts++;return ESP_OK;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) {assert(p==&parts[1]);selects++;return select_error?ESP_FAIL:ESP_OK;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {marks++;running_state=ESP_OTA_IMG_VALID;return ESP_OK;}
const esp_app_desc_t *esp_app_get_description(void) {return &running_desc;}
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d) {assert(p==&parts[1]);memcpy(d,flash_bytes+32,sizeof(*d));return ESP_OK;}
void *esp_flash_default_chip;
esp_err_t esp_flash_get_size(void *chip,uint32_t *out) {(void)chip;*out=flash_size;return ESP_OK;}
int64_t esp_timer_get_time(void) {return now;}
void esp_restart(void) {restarts++;longjmp(restart_exit,1);}
void esp_crt_bundle_attach(void) {}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {assert(wall_time>1735689600);http_calls++;assert(!http_live);http_live++;assert(cfg->buffer_size==1024&&cfg->buffer_size_tx==1024);assert(cfg->disable_auto_redirect);assert(cfg->transport_type==HTTP_TRANSPORT_OVER_SSL);assert(cfg->crt_bundle_attach);http.cfg=*cfg;http.pos=0;if(worker_manifest){assert(log_level==ESP_LOG_NONE);if(strstr(cfg->url,"latest.json")){response.body=(const unsigned char *)manifest_json;response.size=strlen(manifest_json);}else{response.body=image;response.size=image_size;}response.fetched=response.size;snprintf(length_text,sizeof(length_text),"%zu",response.size);response.length=length_text;}return &http;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v) {(void)c;if(!strcmp(k,"Accept-Encoding"))assert(!strcmp(v,"identity"));return ESP_OK;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int len) {(void)c;assert(!len);return ESP_OK;}
static void emit(const char *key,const char *value) {if(value){esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,http.cfg.user_data,(char *)key,(char *)value};http.cfg.event_handler(&e);}}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) {(void)c;emit("Content-Length",response.length);if(response.duplicate_length)emit("content-length",response.length);emit("Content-Encoding",response.encoding);emit("Transfer-Encoding",response.transfer);emit("Location",response.location);if(response.duplicate_location)emit("Location",response.location);return response.fetched;}
int esp_http_client_get_status_code(esp_http_client_handle_t c) {(void)c;return response.status;}
bool esp_http_client_is_chunked_response(esp_http_client_handle_t c) {(void)c;return response.chunked;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) {assert(c==&http&&http_live==1);http_live--;cleanups++;return ESP_OK;}
int esp_http_client_read(esp_http_client_handle_t c,char *buf,int want) {reads++;if(reads==expire_read)now=400000000;if(reads==response.fail_read)return -1;if(reads==cancel_read)firmware_update_cancel();size_t n=response.size-c->pos;if(n>(size_t)want)n=want;if(response.short_read&&n>response.short_read)n=response.short_read;memcpy(buf,response.body+c->pos,n);c->pos+=n;return (int)n;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) {return c->pos==response.size&&!response.incomplete;}

static void hash(const void *b,size_t n,unsigned char *d) {mbedtls_sha256_context s;mbedtls_sha256_init(&s);assert(!mbedtls_sha256_starts(&s,0));assert(!mbedtls_sha256_update(&s,b,n));assert(!mbedtls_sha256_finish(&s,d));mbedtls_sha256_free(&s);}
static um_manifest_t manifest;
static void reset(void) {
 assert(!live_allocations&&!live_bytes&&!http_live);
 alloc_calls=fail_alloc=0;peak_bytes=0;psram_available=true;
 wall_time=1735689601;sntp_starts=sntp_waits=http_calls=sync_at=cancel_wait=0;
 start_result=ESP_OK;wait_result=ESP_ERR_TIMEOUT;expire_clock=frozen_clock=false;
 parts[0]=(esp_partition_t){0,16,0x10000,0x400000};parts[1]=(esp_partition_t){0,17,0x410000,0x400000};parts[2]=(esp_partition_t){1,0,0x810000,0x2000};part_count=3;flash_size=0x1000000;
 memset(&running_desc,0,sizeof(running_desc));strcpy(running_desc.project_name,"coffee_watch_demo");strcpy(running_desc.version,"v0.1.1");running_desc.secure_version=2;running_state=ESP_OTA_IMG_VALID;
 state_error=boot_wrong=begin_error=end_error=write_error=partition_read_error=select_error=0;
 begins=writes=ends=aborts=selects=marks=cleanups=reads=restarts=0;cancel_read=cancel_begin=notify_count=expire_read=0;now=0;flash_used=0;worker_manifest=false;yields=0;
 s_worker=(void *)1;s_mutex=(void *)1;s_cancel=false;s_ready=NULL;s_command=0;memset(&s_status,0,sizeof(s_status));
 memset(&manifest,0,sizeof(manifest));strcpy(manifest.version,"v0.1.2");strcpy(manifest.bin,"coffee-watch-v0.1.2.bin");manifest.build=2;manifest.size=image_size=sizeof(image);
 memset(image,0xa5,sizeof(image));esp_image_header_t h={.magic=ESP_IMAGE_HEADER_MAGIC,.chip_id=ESP_CHIP_ID_ESP32S3,.spi_size=ESP_IMAGE_FLASH_SIZE_16MB};memcpy(image,&h,sizeof(h));esp_app_desc_t d=running_desc;d.magic_word=ESP_APP_DESC_MAGIC_WORD;strcpy(d.version,manifest.version);memcpy(image+32,&d,sizeof(d));hash(image,image_size,manifest.sha256);
 memset(&response,0,sizeof(response));response.body=image;response.size=image_size;response.short_read=7;response.status=200;response.fetched=image_size;snprintf(length_text,sizeof(length_text),"%zu",image_size);response.length=length_text;
}
static esp_err_t download(void) {response_t r={.deadline=1000000};return download_image(&manifest,&parts[1],&r);}
static void ready(void) {assert(download()==ESP_OK);s_ready=&parts[1];s_manifest=manifest;s_status.state=FIRMWARE_UPDATE_READY;}
static void setup_worker_manifest(void) {char hex[65];for(size_t i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",manifest.sha256[i]);snprintf(manifest_json,sizeof(manifest_json),"{\"schema\":1,\"product\":\"coffee-watch\",\"chip\":\"esp32s3\",\"partition_schema\":2,\"version\":\"v0.1.2\",\"tag\":\"v0.1.2\",\"bin\":\"coffee-watch-v0.1.2.bin\",\"size\":%u,\"sha256\":\"%s\"}",manifest.size,hex);worker_manifest=true;}
static void run_worker(void) {notify_count=0;if(!setjmp(worker_exit))worker(NULL);}
static unsigned cases;
#define TEST(name) do {reset();name();assert(!live_allocations&&!live_bytes&&!http_live);cases++;puts("PASS " #name);} while(0)
static void short_reads(void) {for(size_t n=1;n<=4096;n=n==1?7:n==7?287:n==287?288:n==288?4096:4097){reset();response.short_read=n;assert(download()==ESP_OK);assert(begins==1&&ends==1&&!aborts&&!selects);assert(flash_used==image_size&&!memcmp(image,flash_bytes,image_size));assert(s_status.received_bytes==image_size);}}
static void bad_lengths(void) {const char *bad[]={NULL,"","0","-1","+9000"," 9000","9000 ","9000,9000","4294967296","8999","9001"};for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++){reset();response.length=bad[i];assert(download()!=ESP_OK);assert(!begins&&!selects);}reset();response.duplicate_length=true;assert(download()!=ESP_OK&&!begins);reset();manifest.size--;assert(download()==ESP_ERR_INVALID_SIZE&&!begins);}
static void bad_framing(void) {response.transfer="chunked";assert(download()!=ESP_OK&&!begins);reset();response.chunked=true;assert(download()!=ESP_OK&&!begins);reset();response.encoding="gzip";assert(download()!=ESP_OK&&!begins);reset();response.incomplete=true;assert(download()!=ESP_OK&&aborts==1&&!ends);reset();response.size--;assert(download()!=ESP_OK&&aborts==1&&!ends);}
static void hash_mismatch(void) {manifest.sha256[0]^=1;assert(download()==ESP_ERR_INVALID_CRC);assert(begins==1&&aborts==1&&!ends&&!selects);}
static void invalid_prefix(void) {image[32+48]='X';assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();image[12]^=1;assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();image[0]=0;assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();image[3]=0;assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();image[32]=0;assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();memset(image+32+16,'X',32);assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();image[32+4]=3;assert(download()==ESP_ERR_INVALID_RESPONSE&&!begins);reset();manifest.size=287;response.size=287;response.fetched=287;response.length="287";assert(download()==ESP_ERR_INVALID_SIZE&&!begins);}
static void cancellation(void) {s_cancel=true;assert(download()!=ESP_OK&&!begins);reset();cancel_read=2;assert(download()!=ESP_OK&&!begins);reset();cancel_begin=1;assert(download()!=ESP_OK&&aborts==1&&!selects);reset();response.short_read=288;cancel_read=3;assert(download()!=ESP_OK&&aborts==1&&!ends&&!selects);reset();ready();firmware_update_cancel();assert(!s_ready&&s_status.state==FIRMWARE_UPDATE_CANCELLED);assert(firmware_update_activate_and_restart(true)==ESP_ERR_INVALID_STATE&&!selects);}
static void explicit_activation(void) {ready();assert(!selects);assert(firmware_update_activate_and_restart(false)==ESP_ERR_INVALID_STATE&&!selects);if(!setjmp(restart_exit)){firmware_update_activate_and_restart(true);assert(0);}assert(selects==1&&restarts==1&&s_status.state==FIRMWARE_UPDATE_ACTIVATING&&yields>=2);}
static void activation_rehash(void) {ready();flash_bytes[500]^=1;assert(firmware_update_activate_and_restart(true)==ESP_ERR_INVALID_CRC&&!selects&&!s_ready);reset();ready();partition_read_error=1;assert(firmware_update_activate_and_restart(true)==ESP_ERR_INVALID_CRC&&!selects);reset();ready();select_error=1;assert(firmware_update_activate_and_restart(true)==ESP_FAIL&&selects==1&&!restarts);}
static void actual_pending_state(void) {running_state=ESP_OTA_IMG_PENDING_VERIFY;assert(firmware_update_mark_running_valid_after_health(false,true)==ESP_ERR_INVALID_STATE&&!marks);assert(firmware_update_mark_running_valid_after_health(true,false)==ESP_ERR_INVALID_STATE&&!marks);assert(firmware_update_mark_running_valid_after_health(true,true)==ESP_OK&&marks==1);assert(firmware_update_mark_running_valid_after_health(true,true)==ESP_OK&&marks==1);for(int st=0;st<=5;st++){reset();running_state=st;esp_err_t e=firmware_update_mark_running_valid_after_health(true,true);assert((e==ESP_OK)==(st==ESP_OTA_IMG_PENDING_VERIFY||st==ESP_OTA_IMG_VALID));assert(marks==(st==ESP_OTA_IMG_PENDING_VERIFY));}reset();state_error=1;assert(firmware_update_mark_running_valid_after_health(true,true)!=ESP_OK&&!marks);reset();s_status.busy=true;assert(firmware_update_mark_running_valid_after_health(true,true)==ESP_ERR_INVALID_STATE&&!marks);}
static void layout_rejection(void) {assert(layout(NULL,true)==ESP_OK);flash_size>>=1;assert(layout(NULL,true)!=ESP_OK);reset();parts[1].size--;assert(layout(NULL,true)!=ESP_OK);reset();parts[2].address++;assert(layout(NULL,true)!=ESP_OK);reset();parts[3]=(esp_partition_t){1,7,0x20000,0x1000};part_count++;assert(layout(NULL,true)!=ESP_OK);reset();parts[3]=(esp_partition_t){0,18,0x900000,0x1000};part_count++;assert(layout(NULL,true)!=ESP_OK);reset();parts[3]=(esp_partition_t){1,7,0xffffff,2};part_count++;assert(layout(NULL,true)!=ESP_OK);reset();boot_wrong=1;assert(layout(NULL,true)!=ESP_OK);reset();running_state=ESP_OTA_IMG_PENDING_VERIFY;assert(layout(NULL,true)!=ESP_OK&&layout(NULL,false)==ESP_OK);reset();running_desc.project_name[0]='X';assert(layout(NULL,true)!=ESP_OK);}
static void worker_success(void) {setup_worker_manifest();assert(firmware_update_request_check()==ESP_OK);run_worker();assert(s_status.state==FIRMWARE_UPDATE_AVAILABLE&&!s_status.busy&&!begins&&!selects&&log_level==5);reset();setup_worker_manifest();assert(firmware_update_request_download()==ESP_OK);run_worker();assert(s_status.state==FIRMWARE_UPDATE_READY&&!s_status.busy&&s_ready==&parts[1]&&ends==1&&!selects&&log_level==5);reset();setup_worker_manifest();strcpy(running_desc.version,"v0.1.2");assert(firmware_update_request_download()==ESP_OK);run_worker();assert(s_status.state==FIRMWARE_UPDATE_ERROR&&!begins&&!selects);reset();setup_worker_manifest();strcpy(running_desc.version,"abc1234-dirty");assert(firmware_update_request_download()==ESP_OK);run_worker();assert(s_status.state==FIRMWARE_UPDATE_READY&&!selects);}
static void prefix_timeout_regression(void) {setup_worker_manifest();response.short_read=288;expire_read=(int)((strlen(manifest_json)+287)/288)+1;assert(firmware_update_request_download()==ESP_OK);run_worker();fprintf(stderr,"prefix deadline repro: state=%d READY=%d begins=%d writes=%d selects=%d (expected ERROR, not READY)\n",s_status.state,FIRMWARE_UPDATE_READY,begins,writes,selects);assert(s_status.state==FIRMWARE_UPDATE_ERROR);assert(!begins&&!selects);}
static void extra_otadata_regression(void) {parts[3]=(esp_partition_t){ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,0x900000,0x2000};part_count++;esp_err_t err=layout(NULL,true);fprintf(stderr,"extra otadata repro: result=%d (expected NOT_SUPPORTED)\n",err);assert(err==ESP_ERR_NOT_SUPPORTED);}
static void factory_init(void) {s_worker=NULL;parts[0].subtype=0;parts[1].subtype=1;assert(firmware_update_init()==ESP_OK);assert(s_worker&&s_status.state==FIRMWARE_UPDATE_ERROR);assert(strstr(s_status.message,"USB"));assert(firmware_update_mark_running_valid_after_health(true,true)==ESP_ERR_NOT_SUPPORTED);assert(!marks&&!selects&&!begins);assert(firmware_update_request_check()==ESP_ERR_NOT_SUPPORTED);assert(s_status.state==FIRMWARE_UPDATE_ERROR&&!s_status.busy);assert(firmware_update_request_download()==ESP_ERR_NOT_SUPPORTED&&!begins&&!selects);}
static void io_failure_cleanup(void) {begin_error=1;assert(download()!=ESP_OK&&begins==1&&aborts==1&&!writes&&!selects);reset();response.fail_read=50;assert(download()!=ESP_OK&&aborts==1&&!ends&&cleanups==1);reset();write_error=1;assert(download()!=ESP_OK&&aborts==1&&!ends);reset();end_error=1;assert(download()!=ESP_OK&&ends==1&&!aborts&&!selects);reset();now=1000001;assert(download()!=ESP_OK&&!begins&&!selects);}
static void request_and_worker(void) {assert(firmware_update_request_check()==ESP_OK);assert(s_status.busy);assert(firmware_update_request_download()==ESP_ERR_INVALID_STATE);firmware_update_status_t out;assert(firmware_update_get_status(&out)==ESP_OK&&out.state==FIRMWARE_UPDATE_CHECKING);assert(firmware_update_get_status(NULL)==ESP_ERR_INVALID_ARG);/* Bad manifest is consumed by real worker and real parser, not stubbed. */run_worker();assert(!s_status.busy&&s_status.state==FIRMWARE_UPDATE_ERROR&&!selects&&log_level==5);reset();assert(firmware_update_request_download()==ESP_OK);firmware_update_cancel();run_worker();assert(s_status.state==FIRMWARE_UPDATE_CANCELLED&&!s_status.busy&&!selects);}
static void psram_required(void) {
 psram_available=false;
 assert(transport_supported()==ESP_ERR_NOT_SUPPORTED);
 assert(firmware_update_request_check()==ESP_ERR_NOT_SUPPORTED&&!s_status.busy);
 assert(firmware_update_request_download()==ESP_ERR_NOT_SUPPORTED&&!s_status.busy);
 s_worker=NULL;assert(firmware_update_init()==ESP_OK);
 assert(s_status.state==FIRMWARE_UPDATE_ERROR&&s_status.error==ESP_ERR_NOT_SUPPORTED);
 assert(!alloc_calls&&!begins&&!selects);
}
static void allocation_failure_cleanup(void) {
 /* Real worker + parser + download: fail each of the eight explicit PSRAM
  * allocations in turn, including each half of paired URL/buffer allocations. */
 for(unsigned failure=1;failure<=8;failure++){
  reset();setup_worker_manifest();fail_alloc=failure;
  assert(firmware_update_request_download()==ESP_OK);run_worker();
  assert(alloc_calls>=failure&&s_status.state==FIRMWARE_UPDATE_ERROR);
  assert(s_status.error==ESP_ERR_NO_MEM&&!s_status.busy&&!s_ready);
  assert(!live_allocations&&!live_bytes&&!http_live&&!begins&&!selects&&log_level==5);
 }
 reset();setup_worker_manifest();assert(firmware_update_request_download()==ESP_OK);run_worker();
 assert(alloc_calls==8&&s_status.state==FIRMWARE_UPDATE_READY);
 assert(peak_bytes==sizeof(response_t)+3*URL_CAP+CHUNK);
 printf("PSRAM peak explicit OTA buffers: %zu bytes; worker stack: 8192 INTERNAL bytes (plus TCB/control/parser overhead)\n",peak_bytes);
 /* Activation allocation failure must preserve READY for a safe retry and
  * never select boot or reboot; success is covered by explicit_activation. */
 fail_alloc=alloc_calls+1;
 assert(firmware_update_activate_and_restart(true)==ESP_ERR_NO_MEM);
 assert(s_status.state==FIRMWARE_UPDATE_READY&&s_ready==&parts[1]);
 assert(!selects&&!restarts&&!live_allocations&&!live_bytes&&!http_live);
}
static void clock_sync_success(void) {
 setup_worker_manifest();wall_time=0;sync_at=3;
 assert(firmware_update_request_download()==ESP_OK);
 assert(!sntp_starts&&!sntp_waits&&!http_calls); /* request never blocks */
 run_worker();
 assert(s_status.state==FIRMWARE_UPDATE_READY&&!s_status.busy&&ends==1);
 assert(sntp_starts==1&&sntp_waits==3&&http_calls==2&&log_level==5);
 reset();setup_worker_manifest();wall_time=1735689601;
 assert(firmware_update_request_check()==ESP_OK);run_worker();
 assert(s_status.state==FIRMWARE_UPDATE_AVAILABLE&&http_calls==1);
 assert(!sntp_starts&&!sntp_waits);
 /* Boundary value is deliberately not considered a valid clock. */
 reset();setup_worker_manifest();wall_time=1735689600;sync_at=1;wait_result=ESP_OK;
 assert(firmware_update_request_check()==ESP_OK);run_worker();
 assert(s_status.state==FIRMWARE_UPDATE_AVAILABLE&&sntp_starts==1&&sntp_waits==1);
}
static void clock_failures(void) {
 for(unsigned mode=0;mode<9;mode++){
  reset();setup_worker_manifest();wall_time=0;
  if(mode==0)start_result=ESP_FAIL;
  if(mode==2)wait_result=ESP_ERR_INVALID_STATE;
  if(mode==3)cancel_wait=1;
  if(mode==4)wait_result=ESP_OK; /* stale OK with invalid wall time */
  if(mode==5)expire_clock=true;
  if(mode==6)frozen_clock=true; /* explicit wait count bounds broken clocks */
  if(mode==7){cancel_wait=1;sync_at=1;wait_result=ESP_OK;}
  if(mode==8){sync_at=1;wait_result=ESP_FAIL;} /* hard error stays fail closed */
  assert(firmware_update_request_download()==ESP_OK);run_worker();
  assert(s_status.state==((mode==3||mode==7)?FIRMWARE_UPDATE_CANCELLED:FIRMWARE_UPDATE_ERROR));
  assert(!s_status.busy&&!s_ready&&!http_calls&&!begins&&!writes&&!selects);
  assert(!live_allocations&&!live_bytes&&!http_live&&log_level==5);
  assert(sntp_starts==1&&sntp_waits==(mode==0?0:(mode==1||mode==6)?15:1));
 }
 reset();setup_worker_manifest();wall_time=0;
 assert(firmware_update_request_check()==ESP_OK);firmware_update_cancel();run_worker();
 assert(s_status.state==FIRMWARE_UPDATE_CANCELLED&&!sntp_starts&&!sntp_waits&&!http_calls);
 /* Shorter total deadline caps the wait and does not start HTTP. */
 reset();wall_time=0;response_t r={.deadline=2500000};
 assert(clock_ready(&r)==ESP_ERR_TIMEOUT&&sntp_waits==3&&now==2500000);
}
static void cold_boot_health(void) {
 wall_time=0;start_result=ESP_FAIL;running_state=ESP_OTA_IMG_PENDING_VERIFY;
 s_worker=NULL;assert(firmware_update_init()==ESP_OK);
 assert(firmware_update_mark_running_valid_after_health(true,true)==ESP_OK&&marks==1);
 assert(!sntp_starts&&!sntp_waits&&!http_calls&&!begins);
}
int main(int argc,char **argv) {
#ifdef CW_NO_TIME_DATE
 (void)argc;(void)argv;reset();
 assert(transport_supported()==ESP_ERR_NOT_SUPPORTED);
 assert(firmware_update_request_check()==ESP_ERR_NOT_SUPPORTED);
 assert(firmware_update_request_download()==ESP_ERR_NOT_SUPPORTED);
 cold_boot_health();assert(!http_calls);puts("PASS missing HAVE_TIME_DATE fails closed; health independent");return 0;
#endif
if(argc>1&&!strcmp(argv[1],"--repro-prefix-deadline")){reset();prefix_timeout_regression();return 0;}if(argc>1&&!strcmp(argv[1],"--repro-extra-otadata")){reset();extra_otadata_regression();return 0;}TEST(short_reads);TEST(bad_lengths);TEST(bad_framing);TEST(hash_mismatch);TEST(invalid_prefix);TEST(cancellation);TEST(explicit_activation);TEST(activation_rehash);TEST(actual_pending_state);TEST(layout_rejection);TEST(io_failure_cleanup);TEST(request_and_worker);TEST(factory_init);TEST(worker_success);TEST(prefix_timeout_regression);TEST(extra_otadata_regression);TEST(psram_required);TEST(allocation_failure_cleanup);TEST(clock_sync_success);TEST(clock_failures);TEST(cold_boot_health);printf("%u runtime groups passed\n",cases);return 0;}
