"""Exercise the actual application transition helper with mocked I/O boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FlowFollowNativeTests(unittest.TestCase):
    def test_clock_completion_submits_first_draft_trace_and_discovers_rs(self):
        source = (ROOT / 'firmware/demo/main/main.cpp').read_text()
        self.assertIn('flow_follow(before,probe.us,true);', source)
        self.assertIn('flow_follow(before,e.us,e.kind==DE_SCALE||e.kind==DE_RS||e.kind==DE_AUX);', source)
        start = source.index('static void flow_follow(')
        body = source.index('{', start)
        depth = 1
        end = body + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        helper = source[start:end]
        fixture = r'''
#include <cassert>
#include <cstdio>
extern "C" {
#include "brew_record.h"
#include "dropbox_setup.h"
}
#define ESP_LOGI(...) ((void)0)
static cw_flow_t flow;
static cw_record_tracker_t records;
static struct {cw_record_status_t record;bool record_visible;int held,result_page;int64_t page_us;} ui;
static bool rs_requested,trace_stopped;
static unsigned uploads,rs_requests,revision,backups;
static int trace;
static void trace_follow(int64_t) {trace_stopped=flow.stage!=CW_BREW;}
static cw_trace_snapshot_t *probe_trace_snapshot() {assert(trace_stopped);return reinterpret_cast<cw_trace_snapshot_t *>(&trace);}
static void transport_find_rs() {rs_requests++;}
static void backup_record(const cw_brew_record_t &r) {assert(r.revision==uploads+1);backups++;}
extern "C" bool dropbox_setup_record(const cw_brew_record_t *r,cw_trace_snapshot_t *snapshot) {
    uploads++;
    assert(backups==uploads);
    if(!r->complete)assert(snapshot==reinterpret_cast<cw_trace_snapshot_t *>(&trace));
    else assert(snapshot==nullptr);
    return true;
}
extern "C" bool cw_record_event(cw_record_tracker_t *,cw_stage_t before,const cw_flow_t *f,int64_t,cw_brew_record_t *out) {
    if((before==CW_BREW&&f->stage==CW_TDS)||(before==CW_TDS&&f->stage==CW_REPORT)) {
        *out={};out->revision=++revision;out->complete=f->stage==CW_REPORT;snprintf(out->id,sizeof out->id,"test");return true;
    }
    return false;
}
'''
        fixture += helper + r'''
int main() {
    flow.stage=CW_BREW;flow_follow(CW_READY,100,true);
    assert(uploads==0&&rs_requests==0);
    flow.stage=CW_TDS;flow_follow(CW_BREW,200,true);
    assert(uploads==1&&revision==1&&rs_requests==1&&ui.record_visible&&ui.record.state==CW_RECORD_SAVING);
    flow_follow(CW_TDS,300,true);assert(uploads==1&&rs_requests==1);
    flow.stage=CW_REPORT;flow_follow(CW_TDS,400,true);
    assert(uploads==2&&revision==2&&rs_requests==1&&backups==2);
    flow.stage=CW_TDS;flow_follow(CW_BREW,500,true);
    assert(uploads==3&&revision==3&&backups==3);
}
'''
        with tempfile.TemporaryDirectory(prefix='cw-follow-') as temp:
            cpp = Path(temp) / 'follow.cpp'
            cpp.write_text(fixture)
            exe = Path(temp) / 'follow'
            command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror']
            command += ['-I', str(ROOT / 'firmware/demo/main')]
            for component in ['brew_record', 'brew_trace', 'coffee_flow', 'bookoo', 'difluid_rs', 'dropbox_config', 'roast_color']:
                command += ['-I', str(ROOT / 'firmware/components' / component / 'include')]
            subprocess.run(command + [str(cpp), '-o', str(exe)], check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
