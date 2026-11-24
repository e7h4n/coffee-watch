"""Check each pixel column emitted by the firmware across two charge cycles."""
import pathlib
import subprocess
import tempfile
import unittest

INCLUDE = pathlib.Path(__file__).resolve().parents[1] / "firmware/demo/main"


class BatteryGaugeTest(unittest.TestCase):
    def test_charge_frames(self):
        fixture = r'''
#include "battery_gauge.h"
#include <array>
#include <cassert>

int main() {
    BatteryGauge gauge;
    for (int percent = -1; percent <= 100; ++percent) {
        gauge.percent = percent;
        int solid = std::max(0, percent) * 22 / 100;
        int blocks = (22 - solid + 3) / 4;
        for (int tick = 0; tick <= (blocks + 1) * 2; ++tick) {
            for (bool charging : {true, false}) {
                int visible = charging && percent >= 0 ? tick % (blocks + 1) : 0;
                for (int offset_us : {0, 319999}) {
                    gauge.lit_blocks = gauge.sweep_blocks(charging, tick * 320000LL + offset_us);
                    assert(gauge.lit_blocks == visible);
                    std::array<char,22> pixels{};
                    gauge.draw_fill([&](int x, int width, bool sweep) {
                        assert(x >= 0 && width > 0 && x + width <= 22);
                        for (int i = x; i < x + width; ++i) {
                            assert(pixels[i] == 0);
                            pixels[i] = sweep ? '+' : '=';
                        }
                    });
                    for (int x = 0; x < 22; ++x) {
                        if (x < solid) {
                            assert(pixels[x] == '=');
                        } else {
                            int index = x - solid;
                            bool gap = index % 4 == 3 && x < 21;
                            bool lit = index / 4 < visible && !gap;
                            assert(pixels[x] == (lit ? '+' : 0));
                        }
                    }
                }
            }
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "battery-test"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(INCLUDE), "-x", "c++", "-", "-o", str(binary)],
                           input=fixture, text=True, check=True)
            subprocess.run([str(binary)], check=True)
