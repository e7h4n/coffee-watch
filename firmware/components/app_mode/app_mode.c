// SPDX-License-Identifier: MIT
#include "app_mode.h"
void cw_app_init(cw_app_t *a) {a->mode=CW_APP_HOME;a->scale_ready=a->sensor_present=false;}
void cw_app_sensor(cw_app_t *a,bool present,bool setup_open) {
    if(a->mode==CW_APP_HOME&&present&&!a->sensor_present&&!setup_open)a->mode=CW_APP_COLOR;
    a->sensor_present=present;
}
void cw_app_scale(cw_app_t *a,bool connected,bool fresh,bool setup_open,bool brew_idle) {
    bool appeared=connected&&(fresh||!a->scale_ready);
    a->scale_ready=connected;
    if(appeared&&a->mode==CW_APP_HOME&&!setup_open)a->mode=CW_APP_BREW;
    else if(!connected&&a->mode==CW_APP_BREW&&brew_idle)a->mode=CW_APP_HOME;
}
void cw_app_home(cw_app_t *a) {a->mode=CW_APP_HOME;}
bool cw_app_choose(cw_app_t *a,cw_app_mode_t mode) {
    if(a->mode!=CW_APP_HOME)return false;
    if((mode==CW_APP_BREW&&a->scale_ready)||(mode==CW_APP_COLOR&&a->sensor_present)) {a->mode=mode;return true;}
    return false;
}
void cw_app_sensor_lost(cw_app_t *a) {a->sensor_present=false;if(a->mode==CW_APP_COLOR)a->mode=CW_APP_HOME;}
