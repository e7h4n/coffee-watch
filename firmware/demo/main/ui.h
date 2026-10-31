#pragma once
#include "board.h"
#include "demo.h"
struct UiState {int held=0;int carousel_dir=0;unsigned carousel_from=0;float carousel_progress=0;};
// Display-only animation clock; never changes flow or sensor values.
bool ui_tick(const cw_flow_t &flow,int64_t now_us);
void ui_draw(const cw_flow_t &flow,const UiState &ui);
int ui_hit(const cw_flow_t &flow,int x,int y);
bool ui_enabled(const cw_flow_t &flow,int action);
