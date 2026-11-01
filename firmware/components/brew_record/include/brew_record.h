// SPDX-License-Identifier: MIT
#pragma once
#include "coffee_flow.h"
#include <stddef.h>
typedef struct {
    char id[64];unsigned revision,brewer;int64_t captured_us;
    int32_t dose_cg,water_cg,beverage_cg,empty_cg;uint32_t elapsed_ms;
    bool complete;float tds,temperature,refraction,extraction;
} cw_brew_record_t;
typedef struct {char session[40];unsigned sequence;cw_brew_record_t record;} cw_record_tracker_t;
void cw_record_init(cw_record_tracker_t *tracker,const char *session);
// Called only after a sensor event has been accepted by the brewing state machine.
// Ready->Brew allocates identity, Beverage->TDS emits a draft, TDS->Report emits completion.
bool cw_record_event(cw_record_tracker_t *tracker,cw_stage_t before,const cw_flow_t *flow,int64_t us,cw_brew_record_t *out);
// Canonical complete JSON document. created_at is a validated UTC value shared by revisions.
int cw_record_json(const cw_brew_record_t *record,const char *created_at,char *out,size_t capacity);
