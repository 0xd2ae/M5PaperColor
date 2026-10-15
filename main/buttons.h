#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PC_ACTION_PREVIOUS 1
#define PC_ACTION_NEXT 2
#define PC_ACTION_TODAY 4
#define PC_ACTION_SETUP 8
#define PC_ACTION_PAGE 16 /* long B: calendar <-> anniversary page */
typedef struct {
    bool raw[3], stable[3], long_sent[3];
    uint64_t changed[3], pressed[3];
} pc_buttons_t;
/* down: bit 0=A, 1=B, 2=C. Time is monotonic milliseconds. */
unsigned pc_buttons_update(pc_buttons_t *state,unsigned down,uint64_t ms);
typedef struct { int delta; bool today,setup,page; int64_t changed; } pc_actions_t;
void pc_actions_add(pc_actions_t *pending,unsigned actions,int64_t microseconds);
bool pc_actions_take(pc_actions_t *pending,int64_t microseconds,pc_actions_t *out);
