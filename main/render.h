#pragma once
#include "model.h"
void pc_render(uint8_t *pixels, const pc_screen_t *screen, const pc_holidays_t *holidays);
void pc_render_test(uint8_t *pixels);

/* Official QWeather code; unsupported codes use 999 (unknown). */
int pc_render_weather_icon(uint8_t *pixels,int x,int y,int code,int size);
