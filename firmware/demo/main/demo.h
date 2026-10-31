#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "coffee_flow.h"
typedef enum {DE_SCALE_UP,DE_SCALE_DOWN,DE_RS_UP,DE_RS_DOWN,DE_SCALE,DE_RS,DE_WRITE,DE_ERROR} demo_event_kind_t;
typedef struct {demo_event_kind_t kind;unsigned epoch,generation;int64_t us;bool success;union {bookoo_message_t scale;rs_event_t rs;char error[64];} data;} demo_event_t;
void demo_post(const demo_event_t *event);
void transport_init(void);
void transport_find_scale(void);
void transport_find_rs(void);
void transport_drop_rs(void);
void transport_write(const uint8_t packet[6],unsigned generation);
#ifdef __cplusplus
}
#endif
