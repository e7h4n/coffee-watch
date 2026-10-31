#pragma once
#include <M5GFX.h>
struct BoardInput {bool next,select,touch,touch_valid;int x,y;};
void board_init();
BoardInput board_input();
LGFX_Sprite &board_canvas();
void board_present();
void board_dump();
void board_shutdown();
// Estimated percentage from M5PM1 voltage, or -1 on read failure.
int board_battery_percent();
