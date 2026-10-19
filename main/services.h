#pragma once
#include "model.h"
#include "anniversary.h"
#include "esp_err.h"
esp_err_t pc_storage_init(void);
/* Settings, holidays, anniversaries and images live on the TF card; NVS only caches weather. */
bool pc_storage_card(void);
void pc_storage_migrate(void);
char *pc_file_read(const char *path,size_t maximum,size_t *length);
esp_err_t pc_file_write(const char *path,const void *data,size_t length);
/* Replace path with a fully written tmp file. */
esp_err_t pc_file_commit(const char *tmp,const char *path);
void pc_config_load(pc_config_t *config);
esp_err_t pc_config_save(const pc_config_t *config);
void pc_weather_load(pc_weather_t *weather,const char *location);
esp_err_t pc_weather_save(const pc_weather_t *weather);
pc_holidays_t *pc_holidays_load(void);
esp_err_t pc_holidays_save(const char *json,size_t length);
void pc_anniversaries_load(pc_anniversaries_t *list);
esp_err_t pc_anniversaries_save(const pc_anniversaries_t *list);
/* kind: "e6" (packed frame), "jpg" (source) or "json" (editor parameters). */
bool pc_image_path(const char *id,const char *kind,char *out,size_t capacity);
esp_err_t pc_image_read(const char *id,uint8_t *packed);
void pc_image_remove(const char *id);
typedef struct {
    char ssid[33];
    int rssi;
    bool auth;
} pc_wifi_ap_t;
esp_err_t pc_wifi_init(void);
int pc_wifi_scan(pc_wifi_ap_t *out,int max_count);
esp_err_t pc_wifi_connect(pc_config_t *config,int64_t deadline);
void pc_wifi_off(void);
bool pc_sync_time(int64_t deadline);
esp_err_t pc_weather_fetch(const pc_config_t *config,pc_weather_t *weather,int64_t deadline);
esp_err_t pc_city_resolve(pc_config_t *config,int64_t deadline);
esp_err_t pc_http_get(const pc_config_t *config,const char *path,int64_t deadline,char **body);
esp_err_t pc_setup_start(pc_config_t *config);
void pc_setup_stop(void);
bool pc_setup_finished(void);
int64_t pc_setup_activity(void);
/* Anniversary the web page asked to show on the panel; clears the request. */
bool pc_setup_preview(char id[17]);

/* Web previews, implemented by the main task's state (M5PaperColor.c) and safe to call from the
 * setup server while the panel refreshes: drawing is serialised by an internal lock. */
typedef enum { PC_PREVIEW_CALENDAR, PC_PREVIEW_ANNIVERSARY, PC_PREVIEW_DRAFT } pc_preview_kind_t;
typedef struct {
    pc_preview_kind_t kind;
    int year, month;                 /* CALENDAR; 0 means the current month */
    char id[17];                     /* ANNIVERSARY; empty means the selected one */
    const pc_anniversary_t *draft;   /* DRAFT */
    const uint8_t *draft_image;      /* DRAFT: packed image, or NULL to use the stored one */
} pc_preview_t;
typedef struct {
    bool time_valid, weather_valid, weather_stale, offline, card, anniversary_page, celebrate_today;
    int battery_percent, anniversaries, holiday_first, holiday_last;
    int64_t weather_fetched;
    bool fonts[3];                   /* card fonts 18, 24, 36 px */
} pc_device_status_t;
void pc_preview_render(const pc_preview_t *request, uint8_t *pixels);
void pc_device_status(pc_device_status_t *status);
/* Re-read card data the setup server just changed so previews show it at once. */
enum { PC_RELOAD_HOLIDAYS = 1, PC_RELOAD_ANNIVERSARIES = 2 };
void pc_preview_reload(unsigned what);
