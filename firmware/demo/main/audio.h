// SPDX-License-Identifier: MIT
#pragma once
#include "driver/i2c_master.h"
// Optional, speaker-only path: no microphone capture. Failure must not stop brewing.
bool audio_init(i2c_master_bus_handle_t bus);
void audio_chirp();
