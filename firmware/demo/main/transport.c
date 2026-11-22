// SPDX-License-Identifier: MIT
#include "demo.h"
#if CONFIG_CW_LAUNCHER_APP
#include "okou_ble.h"
#include "esp_task_wdt.h"
#include "freertos/task.h"
#endif
#include <stdio.h>
#include <inttypes.h>

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "mbedtls/aes.h"
#include "ck811.h"
#include <time.h>

typedef struct {
    bool connected,ready,closing;uint16_t conn,start,end,command,weight,command_cccd,weight_cccd,command_end,weight_end,last_val,write;
    unsigned epoch,sub_step;rs_stream_t stream;
    bool dose_buffered;bookoo_message_t initial_dose;int64_t initial_dose_us;
} peer_t;
// 0: BOOKOO scale, 1: DiFluid RS, 2: second scale (Lefu CK811, found by its name).
enum {SCALE,RS,AUX,PEERS};
static peer_t peers[PEERS];
// The BOOKOO and RS units verified in this spike; the CK811 is matched by name.
static const ble_addr_t addresses[2]={
    {.type=1,.val={0xa4,0x4e,0x5e,0x4a,0x8c,0xd6}},
    {.type=0,.val={0x0a,0x6c,0xc4,0xee,0xf6,0x48}}};
static ble_addr_t aux_address;
static uint8_t own_type;static unsigned epochs,wanted;static int pending=-1;static bool synced,scanning,requested_scale_restart,discard_rs_connect;
static int gap(struct ble_gap_event*,void*);
static void fail(const char *what,int rc) {
    ESP_LOGE("ble","%s rc=%d",what,rc);
    demo_event_t e={.kind=DE_ERROR,.us=esp_timer_get_time()};snprintf(e.data.error,sizeof e.data.error,"%s (%d)",what,rc);demo_post(&e);
}
static peer_t *peer(uint16_t conn) {for(unsigned i=0;i<PEERS;i++)if(peers[i].connected&&peers[i].conn==conn)return &peers[i];return NULL;}
static unsigned kind(const peer_t *p) {return (unsigned)(p-peers);}
// RS and the CK811 have one notify characteristic; BOOKOO has two.
static unsigned role(peer_t *p) {return kind(p)!=SCALE;}
static void link(peer_t *p,bool up) {
    static const demo_event_kind_t kinds[PEERS][2]={{DE_SCALE_DOWN,DE_SCALE_UP},{DE_RS_DOWN,DE_RS_UP},{DE_AUX_DOWN,DE_AUX_UP}};
    static const char *names[PEERS]={"scale","RS","second scale"};
    demo_event_t e={.kind=kinds[kind(p)][up],.epoch=p->epoch,.us=esp_timer_get_time()};demo_post(&e);
    ESP_LOGI("ble","%s %s epoch=%u conn=%u",names[kind(p)],up?"ready":"down",p->epoch,p->conn);
}
static void scan(void);
static bool decrypt(void *ctx,const uint8_t *cipher,uint8_t *plain,size_t n) {
    static const uint8_t key[16]={0x7a,0x3f,0xe9,0x1c,0x8b,0x5d,0x2a,0x6e,0x4c,0x91,0xf7,0x38,0xb2,0x64,0xd5,0x0a};
    uint8_t iv[16]={0x9e,0x2b,0x7c,0x45,0x1f,0x8a,0x3d,0x6b,0xe4,0x57,0x92,0x0c,0x8f,0x3a,0x6d,0xb1};
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);int rc=mbedtls_aes_setkey_dec(&aes,key,128);
    if(!rc)rc=mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,n,iv,cipher,plain);
    mbedtls_aes_free(&aes);return rc==0;
}
static void rs_result(void *ctx,const rs_event_t *r) {
    peer_t *p=ctx;
    if(r->kind==RS_MEASUREMENT||r->kind==RS_MEASUREMENT_ERROR) {
        demo_event_t e={.kind=DE_RS,.epoch=p->epoch,.us=esp_timer_get_time()};e.data.rs=*r;demo_post(&e);
        ESP_LOGI("ble","RS event tds=%.8f temp=%.5f N=%.8f error=%u flags=%"PRIu32,(double)r->tds_percent,(double)r->temperature_c,(double)r->refraction,r->error_code,r->error_flags);
    } else ESP_LOGW("ble","RS reject kind=%u error=%u",r->kind,r->error);
}
static void ready_link(peer_t *p) {
    p->ready=true;link(p,true);
    if(!role(p)&&p->dose_buffered) {
        demo_event_t e={.kind=DE_SCALE,.epoch=p->epoch,.us=p->initial_dose_us};e.data.scale=p->initial_dose;
        demo_post(&e);p->dose_buffered=false;
        ESP_LOGI("ble","stored dose from initialization cg=%"PRId32,e.data.scale.dose_cg);
    }
}
static int initialized(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *a,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(error->status) {fail("Scale state initialization",error->status);return 0;}
    ready_link(p);return 0;
}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *a,void *ctx);
static int clock_synced(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *a,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(error->status)ESP_LOGW("ble","second scale clock sync status=%d",error->status);
    ready_link(p);return 0;
}
static void subscribe(peer_t *p) {
    uint16_t cccd=role(p)?p->command_cccd:(p->sub_step?p->weight_cccd:p->command_cccd);
    if(!cccd) {fail("Missing notify descriptor",BLE_HS_EINVAL);return;}
    uint8_t b[2]={1,0};int rc=ble_gattc_write_flat(p->conn,cccd,b,2,subscribed,p);if(rc)fail("Subscribe",rc);
}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *a,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(error->status) {fail("Subscribe response",error->status);return 0;}
    if(!role(p)&&p->sub_step++==0)subscribe(p);
    else if(!role(p)) {
        // Official BOOKOO N getFeatureInfo: one stored-dose snapshot, no polling.
        const uint8_t query[6]={3,10,20,0,0,29};
        int rc=ble_gattc_write_flat(conn,p->command,query,6,initialized,p);
        if(rc)fail("Scale state initialization",rc);
    } else if(kind(p)==AUX&&p->write) {
        // The CK811 clock sync Pourista sends on connect; only H:M:S are used by the scale.
        time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
        uint8_t sync[8]={0xF1,0x07,0xE8,0x07,0x05,(uint8_t)local.tm_hour,(uint8_t)local.tm_min,(uint8_t)local.tm_sec};
        // Weight notifications do not depend on it: a rejected sync still readies the scale.
        int rc=ble_gattc_write_flat(conn,p->write,sync,sizeof sync,clock_synced,p);
        if(rc) {ESP_LOGW("ble","second scale clock sync rc=%d",rc);ready_link(p);}
    } else ready_link(p);
    return 0;
}
static int descriptors(uint16_t conn,const struct ble_gatt_error *error,uint16_t value,const struct ble_gatt_dsc *d,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(!error->status&&ble_uuid_cmp(&d->uuid.u,BLE_UUID16_DECLARE(0x2902))==0) {
        // Ranges start at each characteristic value and stop at the next def.
        if(value==p->command)p->command_cccd=d->handle;
        else if(value==p->weight)p->weight_cccd=d->handle;
    } else if(error->status==BLE_HS_EDONE) {
        if(!role(p)&&value==p->command) {
            int rc=ble_gattc_disc_all_dscs(conn,p->weight,p->weight_end,descriptors,p);if(rc)fail("Weight descriptor",rc);
        } else subscribe(p);
    } else if(error->status)fail("Descriptor discovery",error->status);
    return 0;
}
static int characteristics(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_chr *chr,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(!error->status) {
        if(p->last_val==p->command&&p->command)p->command_end=chr->def_handle-1;
        if(p->last_val==p->weight&&p->weight)p->weight_end=chr->def_handle-1;
        p->last_val=chr->val_handle;
        static const uint16_t notify_uuid[PEERS]={0xff12,0xff01,CK811_WEIGHT};
        if(ble_uuid_cmp(&chr->uuid.u,BLE_UUID16_DECLARE(notify_uuid[kind(p)]))==0) {
            if(!(chr->properties&BLE_GATT_CHR_PROP_NOTIFY)||(kind(p)!=AUX&&!(chr->properties&BLE_GATT_CHR_PROP_WRITE))) {fail("Unsupported GATT properties",BLE_HS_EINVAL);return 0;}
            p->command=chr->val_handle;p->command_end=p->end;
        }
        if(kind(p)==AUX&&ble_uuid_cmp(&chr->uuid.u,BLE_UUID16_DECLARE(CK811_COMMAND))==0)p->write=chr->val_handle;
        if(!role(p)&&ble_uuid_cmp(&chr->uuid.u,BLE_UUID16_DECLARE(0xff11))==0) {p->weight=chr->val_handle;p->weight_end=p->end;}
    } else if(error->status==BLE_HS_EDONE) {
        if(!p->command||(!role(p)&&!p->weight)) {fail("Missing characteristic",BLE_HS_EINVAL);return 0;}
        uint16_t end=p->command_end;
        int rc=ble_gattc_disc_all_dscs(conn,p->command,end,descriptors,p);if(rc)fail("Command descriptor",rc);
    } else fail("Characteristic discovery",error->status);
    return 0;
}
static int services(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_svc *svc,void *ctx) {
    peer_t *p=peer(conn);if(!p||p!=ctx||p->closing)return 0;
    if(!error->status) {p->start=svc->start_handle;p->end=svc->end_handle;}
    else if(error->status==BLE_HS_EDONE) {
        if(!p->start) {fail("Missing device service",BLE_HS_EINVAL);return 0;}
        int rc=ble_gattc_disc_all_chrs(conn,p->start,p->end,characteristics,p);if(rc)fail("Characteristic discovery",rc);
    } else fail("Service discovery",error->status);
    return 0;
}
// One scan serves every wanted peer: the BOOKOO and RS by address, the CK811 by name.
static int scan_target(const struct ble_gap_disc_desc *d) {
    for(int i=0;i<AUX;i++)if((wanted>>i&1)&&!peers[i].connected&&!memcmp(&d->addr,&addresses[i],sizeof(ble_addr_t)))return i;
    if((wanted>>AUX&1)&&!peers[AUX].connected) {
        struct ble_hs_adv_fields f;
        if(!ble_hs_adv_parse_fields(&f,d->data,d->length_data)&&f.name&&ck811_name((const char*)f.name,f.name_len)) {aux_address=d->addr;return AUX;}
    }
    return -1;
}
static int gap(struct ble_gap_event *e,void *ctx) {
    switch(e->type) {
    case BLE_GAP_EVENT_DISC: {
        if(pending>=0)break;
        int which=scan_target(&e->disc);if(which<0)break;
        int rc=ble_gap_disc_cancel();scanning=false;if(rc) {fail("End scan",rc);break;}
        pending=which;
        rc=ble_gap_connect(own_type,which==AUX?&aux_address:&addresses[which],10000,NULL,gap,NULL);
        if(rc) {pending=-1;fail("Connect",rc);scan();}
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:scanning=false;break;
    case BLE_GAP_EVENT_CONNECT: {
        int which=pending;pending=-1;
        if(discard_rs_connect&&which==RS) {discard_rs_connect=false;if(!e->connect.status)ble_gap_terminate(e->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);scan();break;}
        if(e->connect.status||which<0) {fail("Connection response",e->connect.status);scan();break;}
        peer_t *p=&peers[which];memset(p,0,sizeof *p);p->connected=true;p->conn=e->connect.conn_handle;p->epoch=++epochs;
        static const uint16_t service_uuid[PEERS]={0x0ffe,0x00fa,CK811_SERVICE};
        int rc=ble_gattc_disc_svc_by_uuid(p->conn,BLE_UUID16_DECLARE(service_uuid[which]),services,p);if(rc)fail("Service discovery",rc);
        scan(); // keep looking for the other wanted peers
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        peer_t *p=peer(e->disconnect.conn.conn_handle);
        if(p) {
            p->connected=p->ready=false;rs_stream_reset(&p->stream);link(p,false);
            if(p==&peers[SCALE]&&requested_scale_restart)requested_scale_restart=false;
            scan(); // scales reconnect by themselves when they come back
        }
        break;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        peer_t *p=peer(e->notify_rx.conn_handle);if(!p||p->closing)break;
        uint8_t b[512];size_t n=OS_MBUF_PKTLEN(e->notify_rx.om);if(n>sizeof b) {fail("Notification too large",BLE_HS_EMSGSIZE);break;}
        os_mbuf_copydata(e->notify_rx.om,0,n,b);
        if(!p->ready) {
            if(kind(p)==SCALE&&(e->notify_rx.attr_handle==p->weight||e->notify_rx.attr_handle==p->command)) {
                bookoo_message_t dose;
                if(bookoo_decode(b,n,&dose)==BOOKOO_PARSE_OK&&dose.kind==BOOKOO_DOSE) {
                    p->initial_dose=dose;p->initial_dose_us=esp_timer_get_time();p->dose_buffered=true;
                }
            }
            break;
        }
        if(kind(p)==RS) {if(e->notify_rx.attr_handle==p->command)rs_stream_feed(&p->stream,b,n,decrypt,NULL,rs_result,p);}
        else if(kind(p)==AUX) {
            demo_event_t event={.kind=DE_AUX,.epoch=p->epoch,.us=esp_timer_get_time()};
            if(e->notify_rx.attr_handle==p->command&&ck811_weight(b,n,&event.data.aux_cg))demo_post(&event);
        }
        else if(e->notify_rx.attr_handle==p->command||e->notify_rx.attr_handle==p->weight) {
            demo_event_t event={.kind=DE_SCALE,.epoch=p->epoch,.us=esp_timer_get_time()};
            if(bookoo_decode(b,n,&event.data.scale)==BOOKOO_PARSE_OK) {
                demo_post(&event);
                if(event.data.scale.kind!=BOOKOO_WEIGHT) {
                    char raw[41];for(unsigned i=0;i<20;i++)sprintf(raw+2*i,"%02X",b[i]);
                    ESP_LOGI("ble","scale event kind=%u state=%u dose=%"PRId32" weight=%"PRId32" ms=%"PRIu32" hex=%s",event.data.scale.kind,event.data.scale.state,event.data.scale.dose_cg,event.data.scale.weight_cg,event.data.scale.elapsed_ms,raw);
                }
            }
        }
        break;
    }
    default:break;
    }
    return 0;
}
// Scans while a wanted peer is missing (50 % duty); restarting resets the controller's
// duplicate filter, so a peer seen while it was not wanted is reported again.
static void scan(void) {
    if(!synced||scanning||pending>=0)return;
    bool missing=false;for(unsigned i=0;i<PEERS;i++)if((wanted>>i&1)&&!peers[i].connected)missing=true;
    if(!missing)return;
    struct ble_gap_disc_params params={.itvl=160,.window=80,.passive=0,.filter_duplicates=1};
    int rc=ble_gap_disc(own_type,BLE_HS_FOREVER,&params,gap,NULL);
    if(rc)fail("Scan",rc);else scanning=true;
}
static void rescan(void) {if(scanning) {ble_gap_disc_cancel();scanning=false;}scan();}
typedef struct {unsigned op,generation;uint8_t packet[6];} command_t;
static QueueHandle_t commands;static struct ble_npl_event command_event;static unsigned write_generation;static bool writing;
static int written(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *a,void *ctx) {
    writing=false;demo_event_t e={.kind=DE_WRITE,.generation=write_generation,.success=!error->status,.us=esp_timer_get_time()};demo_post(&e);return 0;
}
static void execute(struct ble_npl_event *ev) {
    command_t c;
    while(xQueueReceive(commands,&c,0)==pdTRUE) {
        if(c.op==0) {
            // Both scales are always wanted; ready ones repeat their link for a new session.
            if(!synced) {fail("BLE not synced",BLE_HS_ENOTSYNCED);continue;}
            wanted|=1u<<SCALE|1u<<AUX;
            peer_t *p=&peers[SCALE];
            if(p->ready)link(p,true);
            else if(p->connected) {requested_scale_restart=true;ble_gap_terminate(p->conn,BLE_ERR_REM_USER_CONN_TERM);}
            if(peers[AUX].ready)link(&peers[AUX],true);
            scan();
        } else if(c.op==1) {
            if(!synced) {fail("BLE not synced",BLE_HS_ENOTSYNCED);continue;}
            wanted|=1u<<RS;
            if(peers[RS].ready)link(&peers[RS],true);else if(!peers[RS].connected)rescan();
        } else if(c.op==2) {
            if(writing||!peers[SCALE].ready) {demo_event_t e={.kind=DE_WRITE,.generation=c.generation,.success=false,.us=esp_timer_get_time()};demo_post(&e);continue;}
            writing=true;write_generation=c.generation;
            int rc=ble_gattc_write_flat(peers[SCALE].conn,peers[SCALE].command,c.packet,6,written,NULL);
            if(rc) {writing=false;demo_event_t e={.kind=DE_WRITE,.generation=c.generation,.success=false,.us=esp_timer_get_time()};demo_post(&e);}
        } else if(c.op==3) {
            wanted&=~(1u<<RS);
            if(pending==RS) {discard_rs_connect=true;ble_gap_conn_cancel();}
            if(peers[RS].connected) {peers[RS].closing=true;ble_gap_terminate(peers[RS].conn,BLE_ERR_REM_USER_CONN_TERM);}
        }
    }
}
static void submit(command_t c) {configASSERT(xQueueSend(commands,&c,0)==pdTRUE);ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&command_event);}
void transport_find_scale(void) {submit((command_t){.op=0});}
void transport_find_rs(void) {submit((command_t){.op=1});}
void transport_drop_rs(void) {submit((command_t){.op=3});}
void transport_write(const uint8_t packet[6],unsigned generation) {command_t c={.op=2,.generation=generation};memcpy(c.packet,packet,6);submit(c);}
static void sync_host(void) {int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&own_type);if(rc) {fail("BLE sync",rc);return;}synced=true;wifi_setup_synced(own_type);transport_find_scale();}
static void reset_host(int reason) {
    wifi_setup_reset(reason);
    synced=false;scanning=false;pending=-1;writing=false;
    for(unsigned i=0;i<PEERS;i++)if(peers[i].connected) {peers[i].connected=peers[i].ready=false;rs_stream_reset(&peers[i].stream);link(&peers[i],false);}
    fail("BLE host reset",reason);
}
static void host(void *arg) {nimble_port_run();nimble_port_freertos_deinit();}
void transport_init(void) {
#if CONFIG_CW_LAUNCHER_APP
    // Boot-time Wi-Fi provisioning temporarily owns NimBLE before the scale transport.
    while (okou_ble_host_owned()) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#endif
    commands=xQueueCreate(8,sizeof(command_t));configASSERT(commands);
    ESP_ERROR_CHECK(nimble_port_init());wifi_setup_register();ble_npl_event_init(&command_event,execute,NULL);
    ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset_host;nimble_port_freertos_init(host);
}
