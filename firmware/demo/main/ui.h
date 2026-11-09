#pragma once
#include "board.h"
#include "demo.h"
struct UiState {bool wifi_setup=false;cw_network_view_t network={};cw_record_status_t record={};bool record_visible=false;int battery_percent=-1;int held=0;int carousel_dir=0;unsigned carousel_from=0;float carousel_progress=0;
    // Result pages (Waiting TDS / Report). Display-only: turning pages never changes flow.
    int result_page=0;int64_t page_us=0;};
// Waiting TDS: summary, curve. Report: report, TDS/extraction chart, curve.
// The curve page exists once a probe trace reached the brew start.
int ui_result_pages(const cw_flow_t &flow);
bool ui_curve_page(const cw_flow_t &flow,const UiState &ui);
// Display-only animation clock; never changes flow or sensor values.
bool ui_tick(const cw_flow_t &flow,const UiState &ui,int64_t now_us);
void ui_draw(const cw_flow_t &flow,const UiState &ui);
int ui_hit(const cw_flow_t &flow,int x,int y,const UiState *ui=nullptr);
bool ui_enabled(const cw_flow_t &flow,int action,const UiState *ui=nullptr);
