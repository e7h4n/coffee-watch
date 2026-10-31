// SPDX-License-Identifier: MIT
#include "bookoo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bookoo_message_t captured(const char *hex)
{
    uint8_t data[20];unsigned byte;
    assert(strlen(hex)==40);
    for(unsigned i=0;i<20;i++){assert(sscanf(hex+i*2,"%2x",&byte)==1);data[i]=(uint8_t)byte;}
    bookoo_message_t m;assert(bookoo_decode(data,20,&m)==BOOKOO_PARSE_OK);return m;
}
static void receive(bookoo_session_t *s,bookoo_message_t m)
{bookoo_session_receive(s,s->epoch,&m);}
static void prepared(bookoo_session_t *s)
{
    bookoo_session_init(s);bookoo_session_connect(s,1);bookoo_session_begin(s);
    receive(s,captured("030F2B00091A0000000000000000000000000034"));
    receive(s,captured("030D020023F02B00000A2B0000000000000000D5"));
    assert(s->phase==BOOKOO_READY && s->dose_cg==2330 && s->dose_valid);
    assert(s->elapsed_ms==0); // Ready carried the previous brew's 9.2 seconds.
}
static void test_stop_snapshot_from_device_trace(void)
{
    bookoo_session_t s;prepared(&s);
    receive(&s,captured("030D010023F02B00000A2B0000000000000000D6"));
    assert(s.phase==BOOKOO_RUNNING && s.elapsed_ms==0);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.weight_cg=4040,.elapsed_ms=29000});
    receive(&s,captured("030D000071AC2B000FC82B00AA000000000000BE"));
    assert(s.phase==BOOKOO_STOPPED && s.result_valid);
    assert(s.result.elapsed_ms==29100 && s.result.water_cg==4040 && s.result.water_valid);
    assert(s.result.dose_valid && s.result.dose_cg==2330);
    bookoo_result_t frozen=s.result;
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.weight_cg=0,.elapsed_ms=0});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=0,.weight_cg=-100});
    bookoo_session_disconnect(&s);bookoo_session_connect(&s,2);
    assert(memcmp(&frozen,&s.result,sizeof frozen)==0 && s.result_valid);
    assert(s.elapsed_ms==29100 && !s.dose_valid);
}
static void test_interruption_requires_explicit_new_brew(void)
{
    bookoo_session_t s;prepared(&s);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.weight_cg=2000,.elapsed_ms=10000});
    bookoo_session_disconnect(&s);assert(s.phase==BOOKOO_INTERRUPTED);
    bookoo_session_connect(&s,2);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=0,.elapsed_ms=12000});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=2});
    assert(s.phase==BOOKOO_INTERRUPTED && !s.result_valid && s.elapsed_ms==10000);
    assert(s.running_dose_cg==2330); // Retained evidence, not permission to resume.
    bookoo_session_begin(&s);
    bookoo_message_t dose={.kind=BOOKOO_DOSE,.dose_cg=1500};
    bookoo_session_receive(&s,1,&dose);assert(!s.dose_valid); // Old generation.
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=2});
    uint8_t p[6];assert(!bookoo_session_request(&s,BOOKOO_CMD_START,p));
    receive(&s,dose);assert(bookoo_session_request(&s,BOOKOO_CMD_START,p));
}
static void test_commands_wait_for_scale_state(void)
{
    bookoo_session_t s;prepared(&s);uint8_t p[6];
    assert(bookoo_session_request(&s,BOOKOO_CMD_START,p));
    assert(p[0]==3 && p[1]==10 && p[2]==4 && p[5]==13);
    assert(s.phase==BOOKOO_READY && s.pending==BOOKOO_CMD_START);
    assert(!bookoo_session_request(&s,BOOKOO_CMD_START,p));
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.elapsed_ms=0,.weight_cg=0});
    assert(s.pending==BOOKOO_CMD_START && s.phase==BOOKOO_READY);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1});
    assert(s.phase==BOOKOO_RUNNING && !s.pending);
    assert(!bookoo_session_request(&s,(bookoo_command_t)1,p));
    assert(bookoo_session_request(&s,BOOKOO_CMD_STOP,p));
    bookoo_session_write_failed(&s);
    assert(s.phase==BOOKOO_RUNNING && !s.pending && !s.result_valid);
}
static void test_dose_is_frozen_at_start(void)
{
    bookoo_session_t s;prepared(&s);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_DOSE,.dose_cg=0});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=0});
    assert(s.result.dose_valid && s.result.dose_cg==2330);
    bookoo_session_begin(&s);assert(!s.dose_valid && !s.result_valid);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=2});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1}); // Explicit zero dose invalidated the stored value.
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=0});
    assert(s.result_valid && !s.result.dose_valid); // Evidence exists; no valid ratio/report.
}
static void test_decode_failure_does_not_publish_partial_data(void)
{
    uint8_t p[20]={3,15,0x2b,0,9,26};
    for(unsigned i=0;i<19;i++)p[19]^=p[i];
    bookoo_message_t out={.dose_cg=123};
    p[3]^=1;assert(bookoo_decode(p,20,&out)==BOOKOO_BAD_CHECKSUM);assert(out.dose_cg==123);
    p[3]^=1;p[2]=0xff;p[19]=0;
    for(unsigned i=0;i<19;i++)p[19]^=p[i];
    assert(bookoo_decode(p,20,&out)==BOOKOO_BAD_SIGN);assert(out.dose_cg==123);
    assert(bookoo_decode(p,19,&out)==BOOKOO_BAD_LENGTH);
    bookoo_message_t unknown=captured("030E020401020000000000000000000000000008");
    assert(unknown.kind==BOOKOO_UNKNOWN);
}
static void test_removal_uses_settlement_not_negative_stream(void)
{
    bookoo_session_t s;prepared(&s);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.weight_cg=-28190,.elapsed_ms=36000});
    // Actual dripper-removal test: screen 0'36, 99.8g; pre-removal about 100g.
    receive(&s,captured("030D00008E302B0026FC2B01A3000000000000C8"));
    assert(s.result.water_valid && s.result.water_cg==9980 && s.result.elapsed_ms==36400);
    assert(s.live_weight_cg==-28190);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_WEIGHT,.weight_cg=-10,.elapsed_ms=0});
    assert(s.result.water_cg==9980 && s.result.elapsed_ms==36400);
}
static void test_stored_dose_is_connection_owned(void)
{
    bookoo_session_t s;bookoo_session_init(&s);bookoo_session_connect(&s,10);
    uint8_t snapshot[20]={3,0x10,1,1,0,1,0,6,0x9a};
    for(unsigned i=0;i<19;i++)snapshot[19]^=snapshot[i];
    bookoo_message_t stored;assert(bookoo_decode(snapshot,20,&stored)==BOOKOO_PARSE_OK);
    assert(stored.kind==BOOKOO_DOSE&&stored.type==0x10&&stored.dose_cg==1690);
    receive(&s,stored);
    bookoo_session_begin(&s);assert(s.dose_valid&&s.dose_cg==1690);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=2});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=1});
    receive(&s,(bookoo_message_t){.kind=BOOKOO_DOSE,.dose_cg=1850});
    assert(s.dose_cg==1850&&s.running_dose_cg==1690);
    receive(&s,(bookoo_message_t){.kind=BOOKOO_AUTO,.state=0});
    assert(s.result.dose_cg==1690);
    bookoo_session_begin(&s);assert(s.dose_valid&&s.dose_cg==1850);
    bookoo_session_disconnect(&s);bookoo_session_connect(&s,11);bookoo_session_begin(&s);
    assert(!s.dose_valid);
    bookoo_message_t dose={.kind=BOOKOO_DOSE,.dose_cg=1800};
    bookoo_session_receive(&s,10,&dose);assert(!s.dose_valid);
    receive(&s,dose);assert(s.dose_valid&&s.dose_cg==1800);
}
int main(void)
{
    test_stored_dose_is_connection_owned();
    test_stop_snapshot_from_device_trace();
    test_interruption_requires_explicit_new_brew();
    test_commands_wait_for_scale_state();
    test_dose_is_frozen_at_start();
    test_decode_failure_does_not_publish_partial_data();
    test_removal_uses_settlement_not_negative_stream();
    puts("BOOKOO adapter: 7 scenario tests passed");
    return 0;
}
