// SPDX-License-Identifier: MIT
#include "app_mode.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int scenarios=0;cw_app_t a;
    // Boot with the sensor attached: the first poll is a new connection.
    cw_app_init(&a);assert(a.mode==CW_APP_HOME);
    cw_app_sensor(&a,true,false);assert(a.mode==CW_APP_COLOR);
    // The scale connecting in the background never pulls color mode away.
    cw_app_scale(&a,true,true,false,true);assert(a.mode==CW_APP_COLOR&&a.scale_ready);
    // Home keeps both devices as choices without re-entering either.
    cw_app_home(&a);cw_app_sensor(&a,true,false);assert(a.mode==CW_APP_HOME);
    cw_app_scale(&a,true,false,false,true);assert(a.mode==CW_APP_HOME); // a repeated link, not new
    assert(cw_app_choose(&a,CW_APP_BREW)&&a.mode==CW_APP_BREW);
    assert(!cw_app_choose(&a,CW_APP_COLOR)&&a.mode==CW_APP_BREW);
    scenarios++;
    // Scale first: its link enters brewing; a sensor plugged in during a brew waits for Home.
    cw_app_init(&a);cw_app_sensor(&a,false,false);cw_app_scale(&a,true,true,false,true);assert(a.mode==CW_APP_BREW);
    cw_app_home(&a);cw_app_sensor(&a,true,false);assert(a.mode==CW_APP_COLOR);
    scenarios++;
    // Losing the scale at the carousel returns Home, its return re-enters brewing;
    // mid-brew the loss stays an interruption inside brewing.
    cw_app_init(&a);cw_app_scale(&a,true,true,false,true);
    cw_app_scale(&a,false,false,false,true);assert(a.mode==CW_APP_HOME&&!a.scale_ready);
    cw_app_scale(&a,true,true,false,true);assert(a.mode==CW_APP_BREW);
    cw_app_scale(&a,false,false,false,false);assert(a.mode==CW_APP_BREW);
    assert(!cw_app_choose(&a,CW_APP_BREW));
    scenarios++;
    // Wi-Fi setup suspends automatic entry; the device is then a choice.
    cw_app_init(&a);cw_app_sensor(&a,true,true);cw_app_scale(&a,true,true,true,true);assert(a.mode==CW_APP_HOME);
    cw_app_sensor(&a,true,false);assert(a.mode==CW_APP_HOME);
    assert(cw_app_choose(&a,CW_APP_COLOR)&&a.mode==CW_APP_COLOR);
    scenarios++;
    // A removed sensor returns Home; attaching it again re-enters color mode.
    cw_app_sensor_lost(&a);assert(a.mode==CW_APP_HOME&&!a.sensor_present);
    assert(!cw_app_choose(&a,CW_APP_COLOR));
    cw_app_sensor(&a,false,false);cw_app_sensor(&a,true,false);assert(a.mode==CW_APP_COLOR);
    scenarios++;
    // Second scale first, BOOKOO later: each new connection counts on Home.
    cw_app_init(&a);cw_app_scale(&a,true,true,false,true);assert(a.mode==CW_APP_BREW);
    cw_app_home(&a);cw_app_scale(&a,true,true,false,true);assert(a.mode==CW_APP_BREW);
    scenarios++;
    printf("%d app mode scenarios passed\n",scenarios);
    return 0;
}
