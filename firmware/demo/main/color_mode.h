// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
// Roast-color meter, entered instead of the brew assistant when a MAX30101 answers
// on G5/G6 at boot (see spec/colorimeter.md). Scale, RS and Wi-Fi are not started.
// read_command returns the next serial console line, if any. Never returns.
[[noreturn]] void color_mode_run(bool (*read_command)(char *line,size_t size));
