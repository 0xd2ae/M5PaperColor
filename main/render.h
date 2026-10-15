#pragma once
#include "model.h"
#include "anniversary.h"
void pc_render(uint8_t *pixels, const pc_screen_t *screen, const pc_holidays_t *holidays);
void pc_render_test(uint8_t *pixels);

/* Official QWeather code; unsupported codes use 999 (unknown). */
int pc_render_weather_icon(uint8_t *pixels,int x,int y,int code,int size);

typedef struct {
    pc_date_t today;
    bool time_valid, card;
    const pc_anniversary_t *item; /* NULL: no anniversaries */
    pc_anniv_status_t status;
    int index, count, battery_percent;
} pc_anniv_screen_t;
/* packed: the anniversary's 4bpp image, or NULL to fill with the band colour. */
void pc_render_anniversary(uint8_t *pixels, const pc_anniv_screen_t *screen, const uint8_t *packed);
/* Characters of text that no built-in or card font has at size; returns their count. */
size_t pc_render_missing(const char *text, int size, char *out, size_t capacity);
