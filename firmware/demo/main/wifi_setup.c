// SPDX-License-Identifier: MIT
#include "wifi_setup.h"
#include "wifi_packet.h"
#include "demo.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "nvs.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static cw_network_view_t view;
static atomic_bool ble_ready,requested_open;
static uint8_t own_type;
static uint16_t browser=BLE_HS_CONN_HANDLE_NONE,status_handle;
static struct ble_npl_event control_event,notify_event;
static QueueHandle_t network_queue;
enum {OP_START=10,OP_DOWN,OP_IP};
typedef struct {cw_wifi_packet_t packet;int op,reason;char ip[16];} network_command_t;
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x47,0xb6,0x99,0x76,0x4e,0x5b,0x21,0xa6,0x55,0x4f,0x9d,0x9a,0x01,0x10,0xa8,0xc9);
static const ble_uuid128_t config_uuid=BLE_UUID128_INIT(0x47,0xb6,0x99,0x76,0x4e,0x5b,0x21,0xa6,0x55,0x4f,0x9d,0x9a,0x02,0x10,0xa8,0xc9);
static const ble_uuid128_t status_uuid=BLE_UUID128_INIT(0x47,0xb6,0x99,0x76,0x4e,0x5b,0x21,0xa6,0x55,0x4f,0x9d,0x9a,0x03,0x10,0xa8,0xc9);
static cw_network_view_t snapshot(void) {cw_network_view_t v;portENTER_CRITICAL(&lock);v=view;portEXIT_CRITICAL(&lock);return v;}
static void publish(void) {
    demo_event_t e={.kind=DE_WIFI,.us=esp_timer_get_time()};e.data.network=snapshot();demo_post(&e);
    if(ble_ready)ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&notify_event);
}
static void network_state(cw_net_state_t state,int reason,const char *ssid,const char *ip) {
    portENTER_CRITICAL(&lock);view.state=state;view.reason=reason;
    if(ssid)snprintf(view.ssid,sizeof view.ssid,"%s",ssid);
    snprintf(view.ip,sizeof view.ip,"%s",ip?ip:"");portEXIT_CRITICAL(&lock);publish();
}
static void request_state(uint32_t request,cw_net_state_t state,int reason,const char *ssid) {
    portENTER_CRITICAL(&lock);view.request_id=request;view.state=state;view.reason=reason;view.ip[0]=0;
    if(ssid)snprintf(view.ssid,sizeof view.ssid,"%s",ssid);
    portEXIT_CRITICAL(&lock);publish();
}
static bool load_credentials(cw_wifi_packet_t *p) {
    nvs_handle_t h;bool ok=false;
    if(nvs_open("cw_wifi",NVS_READONLY,&h)==ESP_OK) {
        size_t a=sizeof p->ssid,b=sizeof p->password;
        ok=nvs_get_str(h,"ssid",p->ssid,&a)==ESP_OK&&nvs_get_str(h,"pass",p->password,&b)==ESP_OK&&p->ssid[0];nvs_close(h);
    }
    p->op=1;return ok;
}
static esp_err_t save_credentials(const cw_wifi_packet_t *p) {
    nvs_handle_t h;esp_err_t rc=nvs_open("cw_wifi",NVS_READWRITE,&h);if(rc!=ESP_OK)return rc;
    rc=nvs_set_str(h,"ssid",p->ssid);if(rc==ESP_OK)rc=nvs_set_str(h,"pass",p->password);
    if(rc==ESP_OK)rc=nvs_commit(h);
    nvs_close(h);return rc;
}
static esp_err_t erase_credentials(void) {
    nvs_handle_t h;esp_err_t rc=nvs_open("cw_wifi",NVS_READWRITE,&h);if(rc!=ESP_OK)return rc;
    rc=nvs_erase_all(h);if(rc==ESP_OK)rc=nvs_commit(h);
    nvs_close(h);return rc;
}
static bool connect_once(cw_wifi_packet_t *p) {
    wifi_config_t config={0};
    memcpy(config.sta.ssid,p->ssid,strlen(p->ssid));memcpy(config.sta.password,p->password,strlen(p->password));
    config.sta.threshold.authmode=p->password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    config.sta.pmf_cfg.capable=true;config.sta.pmf_cfg.required=false;
    esp_err_t rc=esp_wifi_set_config(WIFI_IF_STA,&config);
    mbedtls_platform_zeroize(&config,sizeof config);
    if(rc==ESP_OK)rc=esp_wifi_connect();
    network_state(rc==ESP_OK?CW_NET_CONNECTING:CW_NET_FAILED,rc,p->ssid,NULL);
    return rc==ESP_OK;
}
static void network_task(void *arg) {
    cw_wifi_packet_t active={0},pending={0};bool attempt=false,changing=false,forgetting=false,credential_pending=false,disconnect_pending=false;
    network_command_t c;
    for(;;) {
        if(xQueueReceive(network_queue,&c,portMAX_DELAY)!=pdTRUE)continue;
        if(c.op==OP_START) {
            if(load_credentials(&active)) {portENTER_CRITICAL(&lock);view.saved=true;portEXIT_CRITICAL(&lock);attempt=connect_once(&active);credential_pending=attempt;}
        } else if(c.op==1) {
            pending=c.packet;changing=true;forgetting=false;
            request_state(c.packet.request_id,CW_NET_CONNECTING,0,pending.ssid);
            if(attempt&&!disconnect_pending) {
                esp_err_t rc=esp_wifi_disconnect();
                if(rc==ESP_OK)disconnect_pending=true;
                if(rc==ESP_ERR_WIFI_NOT_CONNECT)attempt=false;
                else if(rc!=ESP_OK) {changing=false;network_state(CW_NET_FAILED,rc,pending.ssid,NULL);}
            }
            if(changing&&!attempt&&!disconnect_pending) {active=pending;mbedtls_platform_zeroize(&pending,sizeof pending);changing=false;attempt=connect_once(&active);credential_pending=attempt;}
        } else if(c.op==2) {
            esp_err_t rc=erase_credentials();
            if(rc!=ESP_OK)request_state(c.packet.request_id,CW_NET_FAILED,rc,NULL);
            else {
                forgetting=true;changing=false;mbedtls_platform_zeroize(&active,sizeof active);mbedtls_platform_zeroize(&pending,sizeof pending);
                if(attempt&&!disconnect_pending)disconnect_pending=esp_wifi_disconnect()==ESP_OK;
                attempt=false;credential_pending=false;
                portENTER_CRITICAL(&lock);view.saved=false;portEXIT_CRITICAL(&lock);
                request_state(c.packet.request_id,CW_NET_UNCONFIGURED,0,"");
            }
        } else if(c.op==OP_DOWN) {
            bool was_connecting=snapshot().state==CW_NET_CONNECTING;attempt=false;credential_pending=false;disconnect_pending=false;
            if(changing) {active=pending;mbedtls_platform_zeroize(&pending,sizeof pending);changing=false;attempt=connect_once(&active);credential_pending=attempt;}
            else if(forgetting)forgetting=false;
            else {mbedtls_platform_zeroize(active.password,sizeof active.password);network_state(was_connecting?CW_NET_FAILED:CW_NET_DISCONNECTED,c.reason,NULL,NULL);}
        } else if(c.op==OP_IP&&!changing&&!forgetting&&attempt) {
            // Only DHCP's real IP event confirms connection and commits credentials.
            esp_err_t rc=ESP_OK;
            if(credential_pending) {rc=save_credentials(&active);portENTER_CRITICAL(&lock);view.saved=rc==ESP_OK;portEXIT_CRITICAL(&lock);credential_pending=false;}
            network_state(CW_NET_CONNECTED,rc,active.ssid,c.ip);
            mbedtls_platform_zeroize(active.password,sizeof active.password);
        }
        mbedtls_platform_zeroize(&c,sizeof c);
    }
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    network_command_t c={0};
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_START)c.op=OP_START;
    else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED) {c.op=OP_DOWN;c.reason=((wifi_event_sta_disconnected_t*)data)->reason;}
    else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP) {c.op=OP_IP;ip_event_got_ip_t *e=data;snprintf(c.ip,sizeof c.ip,IPSTR,IP2STR(&e->ip_info.ip));}
    else return;
    configASSERT(xQueueSend(network_queue,&c,0)==pdTRUE);
}
static void json_escape(char *out,const char *s) {
    while(*s) {unsigned char c=*s++;if(c=='"'||c=='\\') {*out++='\\';*out++=c;}else if(c<32) {sprintf(out,"\\u%04x",c);out+=6;}else *out++=c;}*out=0;
}
static int access_config(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    struct ble_gap_conn_desc desc;
    if(!snapshot().setup_active||conn!=browser||ble_gap_conn_find(conn,&desc)||!desc.sec_state.authenticated||!desc.sec_state.encrypted)return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint8_t bytes[104];uint16_t size=0;network_command_t c={0};
    if(ble_hs_mbuf_to_flat(ctx->om,bytes,sizeof bytes,&size))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    bool valid=cw_wifi_packet_decode(bytes,size,&c.packet);mbedtls_platform_zeroize(bytes,sizeof bytes);
    if(!valid)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    c.op=c.packet.op;bool queued=xQueueSend(network_queue,&c,0)==pdTRUE;mbedtls_platform_zeroize(&c,sizeof c);
    return queued?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static int access_status(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    if(!snapshot().setup_active||conn!=browser)return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    static const char *states[]={"unconfigured","connecting","connected","failed","disconnected"};
    cw_network_view_t v=snapshot();char ssid[193],json[384];json_escape(ssid,v.ssid);
    int n=snprintf(json,sizeof json,"{\"v\":1,\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"reason\":%d,\"request\":%lu,\"saved\":%s}",states[v.state],ssid,v.ip,v.reason,(unsigned long)v.request_id,v.saved?"true":"false");
    return os_mbuf_append(ctx->om,json,n)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&config_uuid.u,.access_cb=access_config,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&status_uuid.u,.access_cb=access_status,.val_handle=&status_handle,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_READ_AUTHEN|BLE_GATT_CHR_F_NOTIFY},
        {0}}}, {0}};
static void notify(struct ble_npl_event *e) {
    cw_network_view_t v=snapshot();
    if(browser!=BLE_HS_CONN_HANDLE_NONE&&v.paired) {
        uint8_t signal=v.state;struct os_mbuf *om=ble_hs_mbuf_from_flat(&signal,1);
        if(om)ble_gatts_notify_custom(browser,status_handle,om);
    }
}
static int wifi_gap(struct ble_gap_event *e,void *arg);
static void control(struct ble_npl_event *e);
static int advertise(void) {
    struct ble_hs_adv_fields fields={0};fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t*)&service_uuid;fields.num_uuids128=1;fields.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);if(rc)return rc;
    uint8_t addr[6];ble_hs_id_copy_addr(own_type,addr,NULL);char name[24];snprintf(name,sizeof name,"CoffeeWatch-%02X%02X",addr[1],addr[0]);
    struct ble_hs_adv_fields response={0};response.name=(uint8_t*)name;response.name_len=strlen(name);response.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&response);if(rc)return rc;
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    return ble_gap_adv_start(own_type,NULL,BLE_HS_FOREVER,&params,wifi_gap,NULL);
}
static void setup_state(bool active,bool paired,bool new_code) {
    portENTER_CRITICAL(&lock);view.setup_active=active;view.paired=paired;
    if(new_code) {view.passkey=esp_random()%1000000;view.setup_error=0;}
    portEXIT_CRITICAL(&lock);publish();
}
static int wifi_gap(struct ble_gap_event *e,void *arg) {
    if(e->type==BLE_GAP_EVENT_CONNECT) {
        if(!e->connect.status) {
            if(!snapshot().setup_active||!requested_open||browser!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(e->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
            else {browser=e->connect.conn_handle;ble_gap_security_initiate(browser);}
        }
        else if(snapshot().setup_active)advertise();
    } else if(e->type==BLE_GAP_EVENT_PASSKEY_ACTION&&e->passkey.params.action==BLE_SM_IOACT_DISP) {
        struct ble_sm_io key={.action=BLE_SM_IOACT_DISP,.passkey=snapshot().passkey};ble_sm_inject_io(e->passkey.conn_handle,&key);
    } else if(e->type==BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        bool paired=snapshot().setup_active&&requested_open&&!e->enc_change.status&&!ble_gap_conn_find(e->enc_change.conn_handle,&desc)&&desc.sec_state.encrypted&&desc.sec_state.authenticated;
        setup_state(snapshot().setup_active,paired,false);
        if(!paired)ble_gap_terminate(e->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
    } else if(e->type==BLE_GAP_EVENT_DISCONNECT) {
        if(e->disconnect.conn.conn_handle!=browser)return 0;
        browser=BLE_HS_CONN_HANDLE_NONE;
        if(requested_open) {
            if(snapshot().setup_active) {setup_state(true,false,true);advertise();}
            else control(NULL);
        }
    }
    return 0;
}
static void control(struct ble_npl_event *e) {
    if(!ble_ready)return;
    if(requested_open&&!snapshot().setup_active) {
        if(browser!=BLE_HS_CONN_HANDLE_NONE)return;
        setup_state(true,false,true);
        int rc=advertise();if(rc) {portENTER_CRITICAL(&lock);view.setup_error=rc;portEXIT_CRITICAL(&lock);setup_state(false,false,false);}
    } else if(!requested_open&&snapshot().setup_active) {
        setup_state(false,false,false);ble_gap_adv_stop();
        if(browser!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(browser,BLE_ERR_REM_USER_CONN_TERM);
    }
}
void wifi_setup_open(void) {requested_open=true;ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&control_event);}
void wifi_setup_close(void) {requested_open=false;ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&control_event);}
void wifi_setup_reset(int reason) {ble_ready=false;requested_open=false;browser=BLE_HS_CONN_HANDLE_NONE;portENTER_CRITICAL(&lock);view.setup_error=reason;portEXIT_CRITICAL(&lock);setup_state(false,false,false);}
void wifi_setup_synced(uint8_t type) {own_type=type;ble_ready=true;ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&control_event);}
void wifi_setup_register(void) {
    ble_npl_event_init(&control_event,control,NULL);ble_npl_event_init(&notify_event,notify,NULL);
    ble_svc_gap_init();ble_svc_gatt_init();
    configASSERT(ble_gatts_count_cfg(services)==0);configASSERT(ble_gatts_add_svcs(services)==0);
    configASSERT(ble_svc_gap_device_name_set("Coffee Watch")==0);
    ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;
}
void wifi_setup_init(void) {
    network_queue=xQueueCreate(8,sizeof(network_command_t));configASSERT(network_queue);
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    configASSERT(esp_netif_create_default_wifi_sta());
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    configASSERT(xTaskCreate(network_task,"wifi-config",4096,NULL,4,NULL)==pdPASS);
    ESP_ERROR_CHECK(esp_wifi_start());
}
