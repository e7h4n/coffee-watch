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

// Roast-color mode (a MAX30101 answered at boot). Display-only view of the meter.
enum ColorPage {COLOR_WARMUP,COLOR_PLACE,COLOR_MEASURING,COLOR_READING,COLOR_RESULT,COLOR_SATURATED,COLOR_NO_DATA,
    COLOR_CAL_PLACE,COLOR_CAL_SETTLE,COLOR_CAL_STEP,COLOR_CAL_DONE,COLOR_CAL_FAILED};
enum ColorFailure {COLOR_FAIL_TOO_DARK,COLOR_FAIL_MOVED};
struct ColorView {
    ColorPage page=COLOR_WARMUP;
    bool live_valid=false;float live=0,progress=0;
    unsigned count=0;float readings[3]={},mean=0,sd=0;bool retest=false;
    uint32_t warmup_ms=0;
    bool calibrated=false;float soda=0;unsigned pa=0;
    ColorFailure failure=COLOR_FAIL_MOVED;float cal_index=0;
};
// Bottom action: Calibrate, Cancel or Done; enabled reports whether it can run.
const char *ui_color_action(const ColorView &v,bool *enabled);
bool ui_color_tick(const ColorView &v,int64_t now_us);
void ui_color_draw(const ColorView &v,const UiState &ui);
int ui_color_hit(const ColorView &v,int x,int y);
