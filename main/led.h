#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { PC_LED_IDLE, PC_LED_NETWORK, PC_LED_REFRESH, PC_LED_SETUP } pc_led_state_t;
/* Unattended (scheduled) wakes only show battery warnings, to stay dark at night. */
esp_err_t pc_led_init(bool attended);
void pc_led_attended(void);
void pc_led_boot(void);
void pc_led_state(pc_led_state_t state);
void pc_led_battery(int percent);
void pc_led_shutdown(void);
