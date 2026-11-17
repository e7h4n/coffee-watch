"""Native policy + real main.cpp update-modal transitions (not a GUI render test)."""
import math
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FirmwareUiNative(unittest.TestCase):
    def test_policy_and_production_modal(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "C++ compiler required")
        main = (ROOT / "firmware/demo/main/main.cpp").read_text()
        helpers = main[main.index("static bool firmware_lock="):main.index('extern "C" void demo_post')]
        prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "firmware_update.h"
#include "update_policy.h"
constexpr int CW_APP_HOME=0,CW_APP_BREW=1,CW_APP_COLOR=2;
constexpr int CW_HOME=0,CW_BREWER=1,CW_BREW=5;
constexpr int CW_NET_CONNECTED=2;
constexpr int UI_FIRMWARE_BACK=5;
struct {int stage=CW_HOME;bool interrupted=false;} flow;
struct {int mode=CW_APP_HOME;} app;
struct {
 bool firmware_open=false,firmware_primary_enabled=false,wifi_setup=false;
 int held=0,battery_percent=70;
 struct {int state=CW_NET_CONNECTED;} network;
 firmware_update_status_t firmware_update={};
 char firmware_current[32]={},firmware_notice[64]={};
} ui;
static bool render_needed;
static int64_t clock_us=1000000;
static int battery_value=70;
static int begins,ends,checks,downloads,cancels,activations;
static bool admission=true;
static firmware_update_status_t worker={};
static int64_t now_us(){return clock_us;}
static int board_battery_percent(){return battery_value;}
static bool dropbox_setup_maintenance_begin(){++begins;return admission;}
static void dropbox_setup_maintenance_end(){++ends;}
struct app_desc_t {const char *version;};
static const app_desc_t *esp_app_get_description(){static app_desc_t d={"v0.1.12"};return &d;}
extern "C" esp_err_t firmware_update_get_status(firmware_update_status_t *out){*out=worker;return ESP_OK;}
extern "C" esp_err_t firmware_update_request_check(){++checks;worker.state=FIRMWARE_UPDATE_CHECKING;worker.busy=true;return ESP_OK;}
extern "C" esp_err_t firmware_update_request_download(){++downloads;worker.state=FIRMWARE_UPDATE_DOWNLOADING;worker.busy=true;return ESP_OK;}
extern "C" void firmware_update_cancel(){++cancels;if(!worker.busy)worker.state=FIRMWARE_UPDATE_CANCELLED;}
extern "C" esp_err_t firmware_update_activate_and_restart(bool owned){assert(owned);++activations;return ESP_FAIL;}
'''
        suffix = r'''
static void reset(){
 ui={};ui.battery_percent=70;ui.network.state=CW_NET_CONNECTED;
 flow={};app={};worker={};firmware_lock=false;firmware_close_when_idle=false;
 firmware_last_poll=0;begins=ends=checks=downloads=cancels=activations=0;
 admission=true;battery_value=70;clock_us+=1000000;render_needed=false;
}
int main(){
 for(int n=-2;n<=102;++n){assert(cw_update_power_allowed(n)==(n>=30&&n<=100));assert(cw_update_must_cancel_for_power(n)==(n<20||n>100));}
 for(int mask=0;mask<16;++mask)assert(cw_update_idle_allowed(mask&1,mask&2,mask&4,mask&8)==(mask==3));
 reset();flow.stage=CW_BREW;firmware_open();assert(!ui.firmware_open&&begins==0);
 reset();app.mode=CW_APP_BREW;firmware_open();assert(!ui.firmware_open&&begins==0);
 reset();flow.interrupted=true;firmware_open();assert(!ui.firmware_open&&begins==0);
 reset();ui.wifi_setup=true;firmware_open();assert(!ui.firmware_open&&begins==0);
 reset();admission=false;firmware_open();assert(ui.firmware_open&&!firmware_lock&&checks==0&&strstr(ui.firmware_notice,"pending"));firmware_close();assert(!ui.firmware_open&&ends==0);
 reset();ui.network.state=0;firmware_open();assert(firmware_lock&&checks==0&&strstr(ui.firmware_notice,"Wi-Fi"));firmware_close();assert(ends==1);
 reset();firmware_open();assert(firmware_lock&&checks==1&&ui.firmware_open&&worker.busy);firmware_open();assert(checks==1);
 firmware_close();assert(ui.firmware_open&&firmware_lock&&firmware_close_when_idle&&!ui.firmware_primary_enabled&&ends==0&&cancels==1);
 worker.busy=false;worker.state=FIRMWARE_UPDATE_CANCELLED;clock_us+=100000;firmware_sync(clock_us);assert(!ui.firmware_open&&!firmware_lock&&ends==1);
 reset();firmware_open();worker.busy=false;worker.state=FIRMWARE_UPDATE_AVAILABLE;firmware_sync(clock_us,true);
 battery_value=-1;firmware_action(1);assert(downloads==0&&activations==0&&strstr(ui.firmware_notice,"30%"));
 battery_value=29;firmware_action(1);assert(downloads==0);
 battery_value=30;firmware_action(1);assert(downloads==1&&activations==0&&firmware_lock);
 ui.battery_percent=19;firmware_sync(clock_us,true);assert(cancels==1&&firmware_lock&&ui.firmware_open);
 worker.busy=false;worker.state=FIRMWARE_UPDATE_READY;firmware_sync(clock_us,true);
 battery_value=29;firmware_action(1);assert(activations==0);
 battery_value=30;firmware_action(1);assert(activations==1&&firmware_lock);
 firmware_action(UI_FIRMWARE_BACK);assert(!ui.firmware_open&&!firmware_lock&&ends==1);
 reset();firmware_open();firmware_action(1);assert(cancels==1&&checks==1&&downloads==0&&firmware_lock);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            tmp = pathlib.Path(td)
            (tmp / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n")
            source = tmp / "modal.cpp"
            source.write_text(prefix + helpers + suffix)
            binary = tmp / "modal"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(tmp), "-I", str(ROOT / "firmware/demo/main"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_update_labels_fit_smallest_committed_font(self):
        # Real VLW advances, not approximate character counts. text() can shrink
        # to 18px only; a longer label would still clip even with max_width set.
        fonts = (ROOT / "firmware/demo/main/smooth_fonts.h").read_text()
        raw = re.search(r"font_18\[\]=\{(.*?)\};", fonts, re.S).group(1)
        data = bytes(map(int, re.findall(r"\d+", raw)))
        count, _, size, _, ascent, descent = struct.unpack_from(">6I", data)
        advance = {32: max(size, ascent + descent) * 2 // 7}
        for i in range(count):
            glyph = struct.unpack_from(">7I", data, 24 + i * 28)
            advance[glyph[0]] = glyph[3]
        ui_source = (ROOT / "firmware/demo/main/ui.cpp").read_text()
        main_source = (ROOT / "firmware/demo/main/main.cpp").read_text()
        labels = re.findall(r'status="([^"]+)"', ui_source)
        labels += re.findall(r'firmware_notice\("([^"]+)"\)', main_source)
        labels += ["First USB migration required", "Update failed; retry when ready",
                   "Updates need at least 30% battery", "Confirm to restart and install",
                   "Installed v0.1.4294967295", "Restart & install"]
        self.assertGreater(len(labels), 15)
        for label in labels:
            with self.subTest(label=label):
                self.assertLessEqual(sum(advance[ord(c)] for c in label), 330)

    def test_primary_actions_fit_round_viewport(self):
        fonts = (ROOT / "firmware/demo/main/smooth_fonts.h").read_text()
        raw = re.search(r"font_34\[\]=\{(.*?)\};", fonts, re.S).group(1)
        data = bytes(map(int, re.findall(r"\d+", raw)))
        count, _, size, _, ascent, descent = struct.unpack_from(">6I", data)
        glyphs = {32: (32, 0, 0, max(size, ascent + descent) * 2 // 7, 0, 0, 0)}
        for i in range(count):
            glyph = struct.unpack_from(">7i", data, 24 + i * 28)
            glyphs[glyph[0]] = glyph
        for label in ["Cancel", "Restart & install", "Download", "Check update"]:
            ink = [glyphs[ord(c)] for c in label if c != " "]
            top = min(-g[4] for g in ink)
            bottom = max(g[1] - g[4] for g in ink)
            shift = (top + bottom - 1) * .5
            rounded = math.floor(shift + .5) if shift >= 0 else math.ceil(shift - .5)
            baseline = 410 - rounded  # design::button_center_y + ink centering
            x = 234 - sum(glyphs[ord(c)][3] for c in label) / 2
            for char in label:
                _, height, width, advance, above, left, _ = glyphs[ord(char)]
                if height and width:
                    for px in (x + left, x + left + width - 1):
                        for py in (baseline - above, baseline - above + height - 1):
                            with self.subTest(label=label, char=char):
                                self.assertLessEqual((px - 234)**2 + (py - 233)**2, 233**2)
                x += advance

    def test_all_mode_changes_and_shutdown_respect_modal(self):
        main = (ROOT / "firmware/demo/main/main.cpp").read_text()
        self.assertIn("fresh,ui.wifi_setup||ui.firmware_open,idle)", main)
        self.assertIn("cw_app_sensor(&app,color_sensor_probe(),ui.wifi_setup||ui.firmware_open)", main)
        self.assertIn("static void go_home() {\n    if(ui.firmware_open) {firmware_close();return;}", main)
        self.assertIn("idle_limit_us&&!(ui.firmware_open&&ui.firmware_update.busy)", main)
        self.assertIn("esp_task_wdt_reset()", main)
        startup = main[main.index('extern "C" void app_main()'):]
        self.assertLess(startup.index("esp_task_wdt_add(nullptr)"), startup.index("nvs_flash_init()"))
        self.assertLess(startup.index("esp_task_wdt_add(nullptr)"), startup.index("board_init()"))
        self.assertIn("now-health_started>=10000000", main)
        self.assertIn("board_canvas().width()==468&&board_canvas().height()==466", main)


if __name__ == "__main__":
    unittest.main()
