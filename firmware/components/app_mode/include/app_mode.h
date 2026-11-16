// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Which product the watch is: Home decides between the brew assistant (scale) and
// the roast colorimeter (MAX30101 on G5/G6). On Home a *new* connection enters its
// mode, the first one winning: a scale link coming up, or the color sensor appearing
// (including at boot). A device that is still connected when Home is entered does not
// re-enter by itself; Home offers it as a choice instead. Wi-Fi setup suspends
// automatic entry.
typedef enum {CW_APP_HOME,CW_APP_BREW,CW_APP_COLOR} cw_app_mode_t;
typedef struct {cw_app_mode_t mode;bool scale_ready,sensor_present;} cw_app_t;
void cw_app_init(cw_app_t *a);
// Color-sensor presence, polled only while on Home.
void cw_app_sensor(cw_app_t *a,bool present,bool setup_open);
// Scale link changes. brew_idle: the brew flow is at its start (no brew in progress),
// so losing the scale there returns Home; a brew in progress keeps its interruption.
void cw_app_scale(cw_app_t *a,bool connected,bool setup_open,bool brew_idle);
// Explicit Home (both keys held, or Back from the brewer carousel).
void cw_app_home(cw_app_t *a);
// A Home choice; only a connected device can be chosen. Returns whether the mode changed.
bool cw_app_choose(cw_app_t *a,cw_app_mode_t mode);
// The color sensor stopped answering in color mode.
void cw_app_sensor_lost(cw_app_t *a);
#ifdef __cplusplus
}
#endif
