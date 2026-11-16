// Compiles the actual production source, not a reimplementation, into the test SO.
#include "../../../firmware/demo/main/brew_backup.c"
#include <assert.h>

typedef struct {unsigned char blobs[2][sizeof(snapshot_t)];size_t sizes[2];bool has_head;uint32_t head;} store_t;
static store_t disk,pending;
static unsigned fail_op,fail_n,immediate,writes,erases;
static bool opened;
// op: 1=open, 2=get_head, 3=get_blob, 4=set_blob, 5=set_head, 6=commit
static bool fail(unsigned op) {return op==fail_op&&--fail_n==0;}
static unsigned key_bank(const char *key) {
    assert(!strcmp(key,"summary0")||!strcmp(key,"summary1"));
    return !strcmp(key,"summary1");
}
void mock_reset(void) {memset(&disk,0,sizeof disk);memset(&pending,0,sizeof pending);opened=false;fail_op=fail_n=immediate=writes=erases=0;}
void mock_reboot(void) {assert(!opened);memset(&pending,0,sizeof pending);fail_op=fail_n=0;}
void mock_fail(unsigned op,unsigned nth) {fail_op=op;fail_n=nth;}
void mock_immediate(unsigned enabled) {immediate=enabled;}
unsigned mock_writes(void) {return writes;}
unsigned mock_erases(void) {return erases;}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) {
    // Any access to WiFi/Dropbox/BLE namespaces fails this test immediately.
    assert(!strcmp(ns,"cw_brew_backup"));assert(mode==NVS_READONLY||mode==NVS_READWRITE);assert(!opened);
    if(fail(1))return ESP_FAIL;
    opened=true;pending=disk;*h=1;return ESP_OK;
}
void nvs_close(nvs_handle_t h) {assert(opened&&h==1);opened=false;memset(&pending,0,sizeof pending);}
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *out) {
    assert(opened&&h==1&&!strcmp(key,"head"));if(fail(2))return ESP_FAIL;
    if(!disk.has_head)return ESP_ERR_NVS_NOT_FOUND;
    *out=disk.head;return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    assert(opened&&h==1);if(fail(3))return ESP_FAIL;
    unsigned bank=key_bank(key);if(!disk.sizes[bank])return ESP_ERR_NVS_NOT_FOUND;
    if(*size<disk.sizes[bank])return ESP_ERR_NVS_INVALID_LENGTH;
    *size=disk.sizes[bank];memcpy(out,disk.blobs[bank],*size);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) {
    assert(opened&&h==1&&size==sizeof(snapshot_t));writes++;
    unsigned bank=key_bank(key);
    if(fail(4)) {
        // Model an interrupted blob write leaving an invalid candidate on flash.
        memcpy(disk.blobs[bank],in,size/2);disk.sizes[bank]=size/2;
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }
    memcpy(pending.blobs[bank],in,size);pending.sizes[bank]=size;
    if(immediate)disk=pending;
    return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value) {
    assert(opened&&h==1&&!strcmp(key,"head"));writes++;
    if(fail(5))return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    pending.head=value;pending.has_head=true;if(immediate)disk=pending;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(opened&&h==1);if(fail(6))return ESP_FAIL;disk=pending;return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h) {(void)h;erases++;assert(0);return ESP_FAIL;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {(void)h;(void)key;erases++;assert(0);return ESP_FAIL;}

bool fixture_save(const char *id,unsigned revision,int64_t utc,unsigned mode) {
    cw_brew_record_t r={0};snprintf(r.id,sizeof r.id,"%s",id);
    r.revision=revision;r.brewer=2;r.captured_us=123456;r.dose_cg=1500;
    r.water_cg=24000;r.beverage_cg=20000;r.empty_cg=10000;r.elapsed_ms=180000;
    r.complete=true;r.tds=1.4f;r.temperature=25.5f;r.refraction=1.335f;r.extraction=22.4f;
    r.scales=2;r.drain_valid=true;r.drain_start_ms=120000;r.peak_out_cgps=910;
    // Even hostile trace metadata must neither be persisted nor exported.
    memset(r.trace_path,'X',sizeof r.trace_path);r.trace_samples=10000;r.trace_truncated=true;
    switch(mode) {
    case 1:r.tds=NAN;break;
    case 2:r.temperature=INFINITY;break;
    case 3:memset(r.id,'x',sizeof r.id);break;
    case 4:r.brewer=4;break;
    case 5:r.dose_cg=0;break;
    case 6:r.scales=3;break;
    case 7:r.complete=false;r.tds=r.temperature=r.refraction=r.extraction=0;break;
    case 8:r.drain_start_ms=r.elapsed_ms+1;break;
    case 9:r.scales=0;r.drain_valid=false;break;
    case 10:r.water_cg=INT32_MAX;break;
    case 11:r.captured_us=-1;break;
    }
    return brew_backup_save(&r,utc);
}
void mock_corrupt(unsigned mode) {
    assert(disk.has_head&&disk.head<=1);
    unsigned bank=disk.head;snapshot_t s;memcpy(&s,disk.blobs[bank],sizeof s);
    switch(mode) {
    case 0:disk.sizes[bank]--;return;
    case 1:s.checksum^=1;memcpy(disk.blobs[bank],&s,sizeof s);return;
    case 2:s.version++;break;
    case 3:s.magic=0;break;
    case 4:s.count=9;break;
    case 5:memset(s.records[0].id,'x',64);break;
    case 6:s.records[0].tds=NAN;break;
    case 7:s.records[0].revision=0;break;
    case 8:s.records[1]=s.records[0];s.count=2;break;
    case 9:s.records[0].flags=8;break;
    case 10:disk.head=7;return;
    case 11:disk.sizes[bank]=0;return;
    case 12:s.bytes--;break;
    case 13:s.count=0;break;
    }
    s.checksum=checksum(&s);memcpy(disk.blobs[bank],&s,sizeof s);
}
// Prove a failed/ambiguous head commit leaves the prior complete snapshot intact.
unsigned mock_bank_revision(unsigned bank) {
    snapshot_t s;memcpy(&s,disk.blobs[bank],sizeof s);
    if(disk.sizes[bank]!=sizeof s||s.checksum!=checksum(&s))return 0;
    return s.records[0].revision;
}
