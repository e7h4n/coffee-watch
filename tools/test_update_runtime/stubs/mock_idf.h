#ifndef MOCK_IDF_H
#define MOCK_IDF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_INVALID_VERSION 0x10a
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
#define CONFIG_MBEDTLS_HAVE_TIME 1
#ifndef CW_NO_TIME_DATE
#define CONFIG_MBEDTLS_HAVE_TIME_DATE 1
#endif
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1
#define CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP 1
#define CONFIG_LOG_DYNAMIC_LEVEL_CONTROL 1
#define CONFIG_LOG_MAXIMUM_LEVEL 5
typedef int esp_log_level_t;
#define ESP_LOG_NONE 0
esp_log_level_t esp_log_level_get(const char *);
void esp_log_level_set(const char *,esp_log_level_t);
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
esp_err_t esp_netif_sntp_start(void);
esp_err_t esp_netif_sntp_sync_wait(unsigned ticks);
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelay(unsigned);
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_TYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
typedef struct { uint8_t type, subtype; uint32_t address, size; } esp_partition_t;
typedef struct mock_iterator *esp_partition_iterator_t;
const esp_partition_t *esp_partition_find_first(int,int,const char *);
esp_partition_iterator_t esp_partition_find(int,int,const char *);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
void esp_partition_iterator_release(esp_partition_iterator_t);
esp_err_t esp_partition_read(const esp_partition_t *, uint32_t, void *, size_t);
typedef enum {ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED} esp_ota_img_states_t;
typedef unsigned esp_ota_handle_t;
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_boot_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
esp_err_t esp_ota_begin(const esp_partition_t *,size_t,esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t,const void *,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_CHIP_ID_ESP32S3 9
#define ESP_IMAGE_FLASH_SIZE_16MB 4
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
/* Only fields consumed by the SUT; sizes/offsets match IDF wire structures. */
typedef struct __attribute__((packed)) { uint8_t magic, segment_count, spi_mode; uint8_t spi_speed:4, spi_size:4; uint32_t entry_addr; uint8_t wp_pin, spi_pin_drv[3]; uint16_t chip_id; uint8_t rest[10]; } esp_image_header_t;
typedef struct { uint32_t load_addr, data_len; } esp_image_segment_header_t;
typedef struct { uint32_t magic_word, secure_version, reserv1[2]; char version[32], project_name[32], time[16], date[16], idf_ver[32]; uint8_t app_elf_sha256[32]; uint32_t reserv2[20]; } esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *,esp_app_desc_t *);
extern void *esp_flash_default_chip;
esp_err_t esp_flash_get_size(void *,uint32_t *);
int64_t esp_timer_get_time(void);
void esp_restart(void);
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_TRANSPORT_OVER_SSL 2
typedef struct { int event_id; void *user_data; char *header_key, *header_value; } esp_http_client_event_t;
typedef struct { const char *url; int timeout_ms; bool disable_auto_redirect; int buffer_size, buffer_size_tx; esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data; void (*crt_bundle_attach)(void); int transport_type; } esp_http_client_config_t;
typedef struct mock_http *esp_http_client_handle_t;
void esp_crt_bundle_attach(void);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
/* OpenSSL 3 EVP ABI, used instead of IDF mbedTLS. No fabricated digest. */
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_md_st EVP_MD;
EVP_MD_CTX *EVP_MD_CTX_new(void);
void EVP_MD_CTX_free(EVP_MD_CTX *);
const EVP_MD *EVP_sha256(void);
int EVP_DigestInit_ex(EVP_MD_CTX *,const EVP_MD *,void *);
int EVP_DigestUpdate(EVP_MD_CTX *,const void *,size_t);
int EVP_DigestFinal_ex(EVP_MD_CTX *,unsigned char *,unsigned *);
typedef struct { EVP_MD_CTX *ctx; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *s) {s->ctx=EVP_MD_CTX_new();}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *s) {EVP_MD_CTX_free(s->ctx);}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *s,int is224) {(void)is224; return EVP_DigestInit_ex(s->ctx,EVP_sha256(),NULL)==1?0:-1;}
static inline int mbedtls_sha256_update(mbedtls_sha256_context *s,const unsigned char *b,size_t n) {return EVP_DigestUpdate(s->ctx,b,n)==1?0:-1;}
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *s,unsigned char *d) {unsigned n; return EVP_DigestFinal_ex(s->ctx,d,&n)==1 && n==32?0:-1;}
#endif
