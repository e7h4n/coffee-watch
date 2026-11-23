"""Exercise Home input routing and the startup health gate."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HomeInput(unittest.TestCase):
    def test_home_touch_targets(self):
        ui = (ROOT / "firmware/demo/main/ui.cpp").read_text()
        handlers = ui[ui.index("bool ui_enabled("):ui.index("// Probe temperature curve")]
        header = (ROOT / "firmware/demo/main/ui.h").read_text()
        actions = header[header.index("enum {UI_HOME_WIFI"):].split(";", 1)[0] + ";"
        fixture = """
#include <cassert>
enum {CW_HOME,CW_BREWER,CW_EMPTY,CW_BREW};
namespace design { constexpr int button_top=354; }
struct UiState {
    bool home=true, wifi_setup=false, home_scale=true, home_color=true;
};
struct cw_flow_t {
    int stage=CW_HOME;
    bool scale_ready=false, interrupted=false, initial_step=false;
    bool write_pending=false, result_valid=false;
    struct { bool active=false; } stable;
};
"""
        checks = """
int main() {
    cw_flow_t flow;
    UiState ui;
    assert(ui_hit(flow,234,82,&ui)==0);
    assert(ui_hit(flow,234,200,&ui)==UI_HOME_BREW);
    assert(ui_hit(flow,234,280,&ui)==UI_HOME_COLOR);
    assert(ui_hit(flow,234,400,&ui)==UI_HOME_WIFI);
    ui.home_scale=false;
    ui.home_color=false;
    assert(ui_hit(flow,234,200,&ui)==0);
    assert(ui_hit(flow,234,280,&ui)==0);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / "home.cpp"
            binary = source.with_suffix("")
            source.write_text(fixture + actions + "\n" + handlers + checks)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_boot_health(self):
        main = (ROOT / "firmware/demo/main/main.cpp").read_text()
        self.assertIn("esp_task_wdt_reset()", main)
        startup = main[main.index('extern "C" void app_main()'):]
        self.assertLess(startup.index("esp_task_wdt_add(nullptr)"), startup.index("nvs_flash_init()"))
        self.assertLess(startup.index("esp_task_wdt_add(nullptr)"), startup.index("board_init()"))
        self.assertIn("now-health_started>=10000000", main)
        self.assertIn("board_canvas().width()==468&&board_canvas().height()==466", main)


if __name__ == "__main__":
    unittest.main()
