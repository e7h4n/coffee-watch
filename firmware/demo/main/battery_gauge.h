#pragma once
#include <algorithm>
#include <cstdint>

struct BatteryGauge {
    static constexpr int width = 22;
    static constexpr int block_width = 4;
    static constexpr int step_us = 320000;
    int percent = -1;
    int lit_blocks = 0;

    int solid_width() const {
        return width * std::clamp(percent, 0, 100) / 100;
    }

    int sweep_blocks(bool charging, int64_t elapsed_us) const {
        int remaining = width - solid_width();
        int blocks = (remaining + block_width - 1) / block_width;
        return charging && percent >= 0 && remaining > 0
            ? (elapsed_us / step_us) % (blocks + 1) : 0;
    }

    // Offsets are relative to the battery interior. Keep a pixel between sweep blocks.
    template<class Fill>
    void draw_fill(Fill fill) const {
        int solid = solid_width();
        if (solid > 0) fill(0, solid, false);
        for (int block = 0; block < lit_blocks; ++block) {
            int x = solid + block * block_width;
            int left = width - x;
            fill(x, left > block_width ? block_width - 1 : left, true);
        }
    }
};
