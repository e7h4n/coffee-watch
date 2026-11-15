// SPDX-License-Identifier: MIT
#pragma once
#include "ui.h"
// Roast-color meter, one of the two products Home selects (see spec/colorimeter.md).
// The main loop owns timing, events, Wi-Fi/Dropbox and the idle policy.
void color_mode_init(const char *session);
// Entering starts the sensor and its 60 s LED warm-up; leaving stops it (LED off).
void color_mode_enter(int64_t now);
void color_mode_leave();
// Feeds pending sensor samples; true when the meter or page changed (activity).
bool color_mode_tick(int64_t now);
// Gestures; returns true when both keys were held 900 ms (Home).
bool color_mode_input(BoardInput raw,int64_t now,UiState &ui);
// The sensor stopped answering (removed while measuring).
bool color_mode_sensor_lost(int64_t now);
void color_mode_record(const cw_record_status_t &status);
// Color console commands; false when the line is not one of them.
bool color_mode_console(const char *line,int64_t now);
bool color_mode_animating(int64_t now);
void color_mode_draw(const UiState &ui);
