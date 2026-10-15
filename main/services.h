#pragma once
#include "model.h"
#include "esp_err.h"
esp_err_t pc_storage_init(void);
void pc_config_load(pc_config_t *config);
esp_err_t pc_config_save(const pc_config_t *config);
void pc_weather_load(pc_weather_t *weather,const char *location);
esp_err_t pc_weather_save(const pc_weather_t *weather);
pc_holidays_t *pc_holidays_load(void);
esp_err_t pc_holidays_save(const char *json,size_t length);
esp_err_t pc_wifi_init(void);
esp_err_t pc_wifi_connect(const pc_config_t *config,int64_t deadline);
void pc_wifi_off(void);
bool pc_sync_time(int64_t deadline);
esp_err_t pc_weather_fetch(const pc_config_t *config,pc_weather_t *weather,int64_t deadline);
esp_err_t pc_http_get(const pc_config_t *config,const char *path,int64_t deadline,char **body);
esp_err_t pc_setup_start(pc_config_t *config);
void pc_setup_stop(void);
bool pc_setup_finished(void);
int64_t pc_setup_activity(void);
