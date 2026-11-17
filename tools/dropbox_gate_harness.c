// SPDX-License-Identifier: MIT
// RTOS boundary fixture. Production helpers are extracted verbatim by Python.
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/time.h>
#include "dropbox_setup.h"

#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define configASSERT(x) assert(x)
#define ESP_OK 0
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef void *QueueHandle_t;
typedef int *SemaphoreHandle_t;
static int mutex, lock, creates, frees, callbacks;
static bool send_failure, inspect_send, inspect_cleanup, callback_reentry;
static bool inspect_recovery, color_failure;
static unsigned sends, erases;
static const char *last_error;
static void *owned_trace;
static jmp_buf worker_exit;
static void (*task_entry)(void *);
static void tracked_free(void *p) {
    if(p) {assert(p==owned_trace);frees++;owned_trace=NULL;}
    free(p);
}
#define free tracked_free
static void mbedtls_platform_zeroize(void *p,size_t n) {memset(p,0,n);}
static int xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout) {
    assert(m==&mutex);assert(timeout==portMAX_DELAY);assert(!mutex);mutex=1;return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t m) {assert(m==&mutex&&mutex);mutex=0;return pdTRUE;}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {creates++;return &mutex;}
static int xQueueSend(QueueHandle_t,const void *,unsigned);
static int xQueueReceive(QueueHandle_t,void *,unsigned);
static unsigned uxQueueMessagesWaiting(QueueHandle_t);
static QueueHandle_t xQueueCreate(unsigned,size_t);
#include "production_state.inc"

static queued_command_t queue[8];
static unsigned queued;
static unsigned uxQueueMessagesWaiting(QueueHandle_t q) {assert(q==commands);return queued;}
static QueueHandle_t xQueueCreate(unsigned n,size_t size) {
    assert(n==8&&size==sizeof(queued_command_t));return queue;
}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,int priority,void *handle) {
    (void)name;(void)stack;(void)arg;(void)priority;(void)handle;
    task_entry=fn;return pdPASS;
}
static int xQueueSend(QueueHandle_t q,const void *item,unsigned timeout) {
    assert(q==commands&&timeout==0&&!mutex);sends++;
    if(inspect_send) {
        // Reserved, but not yet visible in the RTOS queue. Maintenance must lose.
        assert(atomic_load(&pending)>0);assert(!dropbox_setup_maintenance_begin());
        assert(dropbox_setup_busy());assert(dropbox_setup_unsaved());
    }
    if(send_failure||queued==8)return 0;
    queue[queued++]=*(const queued_command_t *)item;return pdTRUE;
}
static int xQueueReceive(QueueHandle_t q,void *out,unsigned timeout) {
    assert(q==commands&&timeout==portMAX_DELAY&&!mutex);
    if(!queued)longjmp(worker_exit,1);
    *(queued_command_t *)out=queue[0];
    memmove(queue,queue+1,--queued*sizeof *queue);
    assert(!dropbox_setup_maintenance_begin()); // Dequeued, still active.
    return pdTRUE;
}
static void record_status(const cw_brew_record_t *r,cw_record_sync_t state,const char *path,const char *error) {
    (void)r;(void)state;(void)path;
    assert(!mutex);callbacks++;last_error=error;
    if(inspect_cleanup) {
        assert(atomic_load(&pending)>0);assert(!dropbox_setup_maintenance_begin());
    }
    if(callback_reentry) {
        callback_reentry=false;
        cw_dropbox_command_t c={.op=3};
        (void)dropbox_setup_submit(&c); // Deadlocks/asserts if callback holds lock.
        assert(dropbox_setup_busy());
    }
}
static void color_status(const rc_record_t *r,cw_record_sync_t state,const char *path,const char *error) {
    (void)r;record_status(NULL,state,path,error);
}
static struct {unsigned request;} view;
static struct {char bytes[32];} account;
static unsigned active_revision;
static bool load(void) {assert(!mutex);return true;}
static void set_state(const char *s,const char *e) {(void)s;(void)e;assert(!mutex);}
static void wipe_session(void) {
    assert(!mutex);
    if(inspect_cleanup) {assert(atomic_load(&pending)>0);assert(!dropbox_setup_maintenance_begin());}
}
static bool store(bool erase) {assert(!mutex);if(erase)erases++;return true;}
static void begin_auth(const cw_dropbox_command_t *c) {(void)c;assert(!mutex);}
static bool clock_ready(void) {assert(!mutex);return !color_failure;}
static bool refresh(void) {assert(!mutex);return true;}
static bool verify_dropbox(void) {assert(!mutex);return true;}
static bool current(void) {return active_revision==atomic_load(&revision);}
static void run_diagnostic(void) {assert(!mutex);diagnostic=false;}
static const char *upload_record(const cw_brew_record_t *r,cw_trace_snapshot_t *trace) {
    assert(!mutex);
    if(inspect_recovery) {record_file.trace=trace;trace=NULL;strcpy(record_file.pending_hash,"unresolved");}
    free(trace);record_status(r,CW_RECORD_SAVED,NULL,NULL);return NULL;
}
static void upload_color(const rc_record_t *r);
static int64_t esp_timer_get_time(void) {return 0;}
int rc_record_json(const rc_record_t *r,int64_t t,char *out,size_t n) {
    (void)r;(void)t;snprintf(out,n,"{}");return 2;
}
static int upload(const char *p,const char *m,const char *j,size_t n,char *rev,size_t cap) {
    (void)p;(void)m;(void)j;(void)n;assert(!mutex);snprintf(rev,cap,"confirmed");return 0;
}
static const char *upload_error(int result,const char *a,const char *b,const char *c,const char *d) {
    (void)result;(void)a;(void)b;(void)c;(void)d;return NULL;
}
typedef struct {bool start;} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) ((esp_sntp_config_t){.start=true})
static int esp_netif_sntp_init(const esp_sntp_config_t *c) {assert(!c->start);return ESP_OK;}
static void dropbox_task(void *arg);
#include "production_functions.inc"

static cw_brew_record_t brew={.id="brew",.revision=1};
static rc_record_t color={.id="color",.revision=1};
static cw_trace_snapshot_t *trace(void) {
    assert(!owned_trace);owned_trace=calloc(1,sizeof(cw_trace_snapshot_t));assert(owned_trace);return owned_trace;
}
static void reset(void) {
    assert(!mutex);clear_record();assert(!owned_trace);
    queued=0;callbacks=frees=0;sends=erases=0;last_error=NULL;
    send_failure=inspect_send=inspect_cleanup=callback_reentry=inspect_recovery=color_failure=false;
    admission=&mutex;commands=queue;maintenance=false;task_entry=NULL;
    atomic_store(&admission_ready,true);atomic_store(&pending,0);
    atomic_store(&occupied,false);atomic_store(&have_credentials,true);atomic_store(&revision,7);
}
static queued_command_t dequeue(void) {
    assert(queued);queued_command_t item=queue[0];memmove(queue,queue+1,--queued*sizeof *queue);return item;
}
static void drain_worker(void) {
    // The actual worker starts with one initialization reservation.
    reserve_work();
    if(!setjmp(worker_exit))dropbox_task(NULL);
    assert(!mutex&&queued==0&&atomic_load(&pending)==0);
}
static void assert_idle(void) {
    assert(atomic_load(&pending)==0&&!dropbox_setup_busy()&&!dropbox_setup_unsaved());
    assert(dropbox_setup_maintenance_begin());assert(!dropbox_setup_maintenance_begin());
    dropbox_setup_maintenance_end();
}
int main(void) {
    reset();atomic_store(&admission_ready,false);
    assert(!dropbox_setup_maintenance_begin()&&dropbox_setup_busy()&&dropbox_setup_unsaved());
    for(int op=1;op<=4;op++) {cw_dropbox_command_t c={.op=op};assert(!dropbox_setup_submit(&c));}
    assert(!dropbox_setup_record(&brew,trace())&&frees==1);
    assert(!dropbox_setup_color(&color));assert(atomic_load(&revision)==7&&sends==0);
    dropbox_setup_maintenance_end();dropbox_setup_init();assert(creates==1&&task_entry);
    assert(!dropbox_setup_maintenance_begin());
    if(!setjmp(worker_exit))task_entry(NULL);
    assert_idle();

    reset();assert(dropbox_setup_maintenance_begin());
    atomic_store(&have_credentials,false);callback_reentry=true;
    for(int op=1;op<=4;op++) {cw_dropbox_command_t c={.op=op};assert(!dropbox_setup_submit(&c));}
    assert(!dropbox_setup_record(&brew,trace()));assert(frees==1&&!strcmp(last_error,"maintenance_busy"));
    assert(!dropbox_setup_color(&color)&&!strcmp(last_error,"maintenance_busy"));
    assert(atomic_load(&revision)==7&&sends==0&&erases==0&&atomic_load(&pending)==0);
    assert(dropbox_setup_busy()&&!dropbox_setup_unsaved());dropbox_setup_maintenance_end();assert_idle();

    // All four command paths: pre-send reservation, queued, active, post-cleanup.
    for(int op=1;op<=4;op++) {
        reset();inspect_send=true;cw_dropbox_command_t c={.op=op};assert(dropbox_setup_submit(&c));
        assert(atomic_load(&pending)==1&&!dropbox_setup_maintenance_begin());
        queued_command_t item=dequeue();(void)item;
        assert(queued==0&&!dropbox_setup_maintenance_begin());finish_work();assert_idle();
        reset();inspect_send=true;send_failure=true;assert(!dropbox_setup_submit(&c));assert_idle();
    }
    reset();inspect_send=true;
    assert(dropbox_setup_record(&brew,trace()));assert(dropbox_setup_color(&color));
    assert(atomic_load(&pending)==2);queued_command_t item=dequeue();
    assert(!dropbox_setup_maintenance_begin());free(item.trace);finish_work();
    (void)dequeue();assert(!dropbox_setup_maintenance_begin());finish_work();assert(frees==1);assert_idle();

    reset();inspect_send=inspect_cleanup=true;send_failure=true;callback_reentry=true;
    assert(!dropbox_setup_record(&brew,trace()));assert(frees==1&&callbacks==1);assert_idle();
    assert(!dropbox_setup_color(&color));assert(callbacks==2);assert_idle();
    reset();atomic_store(&have_credentials,false);assert(!dropbox_setup_record(&brew,trace()));
    assert(frees==1&&!strcmp(last_error,"authorize_first"));assert_idle();
    assert(!dropbox_setup_color(&color));assert_idle();

    // Queue capacity failure balances only its own reservation; transferred trace
    // remains worker-owned on success and is freed exactly once on all failures.
    reset();for(unsigned i=0;i<8;i++)assert(dropbox_setup_color(&color));
    inspect_cleanup=true;assert(!dropbox_setup_record(&brew,trace()));assert(frees==1&&atomic_load(&pending)==8);
    inspect_cleanup=false;drain_worker();assert_idle();

    // Actual worker, stale-authorization branch, callbacks/reentry and cleanup.
    reset();assert(dropbox_setup_record(&brew,trace()));assert(dropbox_setup_color(&color));
    cw_dropbox_command_t revoke={.op=2}; // revoke is intentionally refused with queued work
    assert(!dropbox_setup_submit(&revoke));atomic_fetch_add(&revision,1);
    inspect_cleanup=true;callback_reentry=true;drain_worker();assert(frees==1);assert_idle();
    reset();assert(dropbox_setup_record(&brew,trace()));inspect_cleanup=true;drain_worker();assert(frees==1);assert_idle();

    // Retained state is never read while pending > 0. The actual worker leaves a
    // failed recovery slot alive after cleanup; maintenance must not discard it.
    reset();assert(dropbox_setup_record(&brew,trace()));inspect_recovery=true;drain_worker();
    assert(record_file.trace&&dropbox_setup_unsaved()&&dropbox_setup_busy());
    assert(!dropbox_setup_maintenance_begin()&&frees==0);clear_record();assert(frees==1);assert_idle();
    reset();strcpy(record_file.pending_hash,"pending-json");assert(!dropbox_setup_maintenance_begin());
    assert(dropbox_setup_unsaved());record_file.pending_hash[0]=0;
    strcpy(record_file.trace_pending_hash,"pending-trace");assert(!dropbox_setup_maintenance_begin());
    record_file.trace_pending_hash[0]=0;
    // Actual upload confirmation/retention is covered by the reliability fixture.
    // Here exercise the real public admission API against that resulting state.
    strcpy(record_file.json_hash,"confirmed");strcpy(record_file.trace_hash,"confirmed");
    record_file.trace=trace();assert_idle();assert(frees==0&&record_file.trace);
    record_file.brew_pending=true; // new revision failed before setting any hash
    assert(dropbox_setup_unsaved()&&dropbox_setup_busy()&&!dropbox_setup_maintenance_begin());
    assert(frees==0&&record_file.trace);
    record_file.brew_pending=false;assert_idle(); // confirmed again; cache untouched
    clear_record();assert(frees==1);assert_idle();

    // Exercise production upload_color's early failure and successful retry.
    reset();color_failure=true;assert(dropbox_setup_color(&color));drain_worker();
    assert(record_file.color_pending&&dropbox_setup_unsaved()&&!dropbox_setup_maintenance_begin());
    color_failure=false;assert(dropbox_setup_color(&color));drain_worker();assert(!record_file.color_pending);assert_idle();
    reset();puts("all Dropbox gate tests passed");return 0;
}
