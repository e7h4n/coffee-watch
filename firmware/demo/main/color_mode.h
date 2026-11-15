// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include "demo.h"
// Roast-color meter, entered instead of the brew assistant when a MAX30101 answers
// on G5/G6 at boot (see spec/colorimeter.md). It joins the saved Wi-Fi and uploads
// every reading to the saved Dropbox app folder; scale, RS and Bluetooth setup are
// not started. read_command returns the next serial console line and read_event
// the next Wi-Fi/record event, if any. Never returns.
[[noreturn]] void color_mode_run(bool (*read_command)(char *line,size_t size),bool (*read_event)(demo_event_t *event));
