// Execute the production HTTP transport function against deterministic ESP stubs.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RESPONSE_MAX 8192
#define TOKEN_URL "https://api.dropboxapi.com/oauth2/token"
#define UPLOAD_URL "https://content.dropboxapi.com/2/files/upload"
#define ESP_OK 0
#define ESP_ERR_NO_MEM -2
#define HTTP_METHOD_POST 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define esp_crt_bundle_attach NULL
static char response[RESPONSE_MAX],access_token[2049]="secret-value",last_log[512];
static int setup_calls,fail_at,opens,writes,cleanups,closes,reads;
static int expected_length=6,max_write,accepted_bytes;
static const char *expected_body;
static bool init_fail,alloc_fail,partial,open_fail,incomplete;
typedef int esp_err_t;
typedef void *esp_http_client_handle_t;
typedef struct {const char *url;int timeout_ms;void *crt_bundle_attach;bool disable_auto_redirect;int buffer_size,buffer_size_tx;} esp_http_client_config_t;
static void log_message(const char *fmt,...) {va_list a;va_start(a,fmt);vsnprintf(last_log,sizeof last_log,fmt,a);va_end(a);}
#define ESP_LOGI(tag,...) log_message(__VA_ARGS__)
#define ESP_LOGW(tag,...) log_message(__VA_ARGS__)
static void mbedtls_platform_zeroize(void *p,size_t n) {memset(p,0,n);}
static const char *esp_err_to_name(int e) {return e==ESP_OK?"OK":"NO_MEM";}
static size_t heap_caps_get_free_size(int caps) {(void)caps;return 10000;}
static size_t heap_caps_get_largest_free_block(int caps) {(void)caps;return 5000;}
static int64_t esp_timer_get_time(void) {return 1000;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    assert(c->disable_auto_redirect);
    assert(c->timeout_ms==(!strcmp(c->url,"https://api.dropboxapi.com/2/files/get_metadata")?45000:15000));
    return init_fail?NULL:(void*)1;
}
static int esp_http_client_set_method(void *h,int method) {assert(h&&method==HTTP_METHOD_POST);return ++setup_calls==fail_at?ESP_ERR_NO_MEM:ESP_OK;}
static int esp_http_client_set_header(void *h,const char *key,const char *value) {assert(h&&key&&value);return ++setup_calls==fail_at?ESP_ERR_NO_MEM:ESP_OK;}
static int esp_http_client_open(void *h,int n) {assert(h&&n==expected_length);opens++;return open_fail?-1:ESP_OK;}
static int esp_http_client_write(void *h,const char *body,int n) {
    assert(h&&n>0&&n<=1024);if(expected_body)assert(body==expected_body+accepted_bytes);
    if(n>max_write)max_write=n;writes++;
    int accepted=partial?(writes==1?3:-1):n;
    if(accepted>0)accepted_bytes+=accepted;
    return accepted;
}
static int esp_http_client_fetch_headers(void *h) {assert(h);return 2;}
static int esp_http_client_read(void *h,char *out,int n) {assert(h&&n>2);if(reads++)return 0;memcpy(out,"{}",2);return 2;}
static bool esp_http_client_is_complete_data_received(void *h) {assert(h);return !incomplete;}
static int esp_http_client_get_status_code(void *h) {assert(h);return 200;}
static void esp_http_client_close(void *h) {assert(h);closes++;}
static void esp_http_client_cleanup(void *h) {assert(h);cleanups++;}
static void *test_malloc(size_t n) {return alloc_fail?NULL:malloc(n);}
#define malloc test_malloc
#include "production_http.inc"
#undef malloc
static void reset(void) {setup_calls=fail_at=opens=writes=cleanups=closes=reads=0;max_write=accepted_bytes=0;expected_length=6;expected_body=NULL;init_fail=alloc_fail=partial=open_fail=incomplete=false;last_log[0]=0;}
static int upload_test(void) {return http(UPLOAD_URL,"abcdef",6,"application/octet-stream",true,"private-api-arg");}
int main(void) {
    for(int step=1;step<=4;step++) {
        reset();fail_at=step;assert(upload_test()==-1);assert(!opens&&!writes&&cleanups==1&&!closes);
        assert(strstr(last_log,"request_bytes=6 sent_bytes=0")&&!strstr(last_log,"secret-value")&&!strstr(last_log,"private-api-arg"));
    }
    reset();alloc_fail=true;assert(upload_test()==-1);assert(!opens&&!writes&&cleanups==1&&setup_calls==3);assert(strstr(last_log,"setup=authorization"));
    reset();init_fail=true;assert(upload_test()==-1&&!opens&&!writes&&!cleanups);assert(strstr(last_log,"setup=init"));
    puts("PASS production method/header/init/allocation failures stop before open/send, no credential logging");
    reset();partial=true;assert(upload_test()==-1&&opens==1&&writes==2&&cleanups==1&&closes==1);assert(strstr(last_log,"request_bytes=6 sent_bytes=3"));
    reset();open_fail=true;assert(upload_test()==-1&&opens==1&&!writes);assert(strstr(last_log,"sent_bytes=0"));
    reset();incomplete=true;assert(upload_test()==-1&&writes==1);assert(strstr(last_log,"request_bytes=6 sent_bytes=6"));
    puts("PASS partial write/open failure/incomplete response keep failure and explicit intended/accepted byte counts");
    reset();assert(upload_test()==200);assert(strstr(last_log,"op=upload")&&strstr(last_log,"sent_bytes=6"));
    reset();assert(http("https://api.dropboxapi.com/2/files/get_metadata","abcdef",6,"application/json",true,NULL)==200);assert(strstr(last_log,"op=metadata"));
    reset();assert(http(TOKEN_URL,"abcdef",6,"application/x-www-form-urlencoded",false,NULL)==200);assert(strstr(last_log,"op=token")&&!strstr(last_log,"secret-value"));
    puts("PASS production HTTP success distinguishes token/upload/metadata operations");
    reset();char large[46797];memset(large,'x',sizeof large);
    expected_length=sizeof large;expected_body=large;
    assert(http(UPLOAD_URL,large,sizeof large,"application/octet-stream",true,"private-api-arg")==200);
    assert(accepted_bytes==sizeof large&&max_write==1024&&writes==46);
    assert(strstr(last_log,"request_bytes=46797 sent_bytes=46797"));
    puts("PASS 46797-byte CSV uses bounded 1024-byte writes with exact byte ordering and accounting");
    return 0;
}
