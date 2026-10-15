#pragma once
#include "model.h"
#include "esp_err.h"
#define PC_KEY_A 10
#define PC_KEY_B 9
#define PC_KEY_C 1
#define PC_KEY_MASK ((1ULL << PC_KEY_A) | (1ULL << PC_KEY_B) | (1ULL << PC_KEY_C))
esp_err_t pc_board_init(void);
esp_err_t pc_epd_refresh(const uint8_t *pixels);
esp_err_t pc_board_quiet(void);
/* SD shares SPI2 with the panel; power is PYG3 plus PYG4 (detect pull-up). */
esp_err_t pc_board_sd_power(bool on);
/* PYG1 level; polarity is not documented, so callers only log it. */
esp_err_t pc_board_sd_detect(bool *low);
esp_err_t pc_rtc_read(time_t *epoch);
esp_err_t pc_rtc_write(time_t epoch);
esp_err_t pc_rtc_arm(unsigned seconds);
int pc_battery_percent(void);
esp_err_t pc_board_led_power(bool on);
esp_err_t pc_climate_read(int *temperature_x10,int *humidity);
