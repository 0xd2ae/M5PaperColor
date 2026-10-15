#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#define PC_WIDTH 400
#define PC_HEIGHT 600
#define PC_PIXELS (PC_WIDTH * PC_HEIGHT)
#define PC_HOLIDAY_MAX_JSON 60000
#define PC_WEATHER_DAYS 3

typedef enum { PC_BLACK, PC_WHITE, PC_RED, PC_YELLOW, PC_BLUE, PC_GREEN } pc_color_t;
typedef struct { int year, month, day; } pc_date_t;
typedef struct { int year, month, day; bool leap; } pc_lunar_t;
typedef enum { PC_NORMAL, PC_REST, PC_WORK } pc_day_kind_t;
typedef struct {
    char ssid[33], password[65], host[128], api_key[129];
    char city[97], location[33];
    double latitude, longitude;
    bool coordinates;
} pc_config_t;
typedef struct { char date[11], text[64]; int icon, low, high; } pc_forecast_t;
typedef struct {
    bool valid;
    int temperature, icon;
    char text[64], observation[40], location[33];
    pc_forecast_t days[PC_WEATHER_DAYS];
    int64_t fetched;
} pc_weather_t;
typedef struct {
    pc_date_t today;
    int view_year, view_month;
    bool time_valid, offline, configuring, indoor_valid;
    int battery_percent;
    int indoor_temperature, indoor_humidity; /* whole °C and % */
    char city[97], updated[32];
    pc_weather_t weather;
} pc_screen_t;
typedef struct pc_holidays pc_holidays_t;
int pc_month_days(int year, int month);
bool pc_date_valid(pc_date_t d);
int pc_day_number(pc_date_t d);
pc_date_t pc_date_from_day(int day);
int pc_weekday(pc_date_t d); /* Monday = 0 */
bool pc_parse_date(const char *s, pc_date_t *out);
void pc_shift_month(int *year, int *month, int delta);
bool pc_lunar_lookup(pc_date_t date, pc_lunar_t *out);
void pc_lunar_label(pc_date_t date, bool full, char *out, size_t capacity);
const char *pc_festival(pc_date_t date);
const char *pc_solar_term(pc_date_t date);
pc_holidays_t *pc_holidays_parse(const char *json, size_t length, char *error, size_t capacity);
void pc_holidays_free(pc_holidays_t *table);
bool pc_holidays_has_year(const pc_holidays_t *table, int year);
pc_day_kind_t pc_holidays_kind(const pc_holidays_t *table, pc_date_t date, const char **name);
bool pc_host_valid(const char *host);
bool pc_location_valid(const char *value);
bool pc_config_valid(const pc_config_t *config);
bool pc_weather_parse(const char *now, const char *daily, const char *location,
                      int64_t fetched, pc_weather_t *out);
uint32_t pc_frame_hash(const uint8_t *pixels, size_t length);
