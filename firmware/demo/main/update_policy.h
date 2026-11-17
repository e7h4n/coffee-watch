// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
// Pure policy, shared by the UI integration and native boundary tests. Admission
// to the Dropbox worker is separately serialized by its maintenance gate.
static inline bool cw_update_idle_allowed(bool home, bool idle_stage, bool interrupted, bool wifi_setup) {
    return home && idle_stage && !interrupted && !wifi_setup;
}
static inline bool cw_update_power_allowed(int battery_percent) {
    // USB presence is not measured on this board; never infer it from a cable
    // prompt or treat an unknown battery reading as externally powered.
    return battery_percent >= 30 && battery_percent <= 100;
}
static inline bool cw_update_must_cancel_for_power(int battery_percent) {
    return battery_percent < 20 || battery_percent > 100;
}
