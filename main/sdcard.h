#pragma once
#include <stdbool.h>
#include "esp_err.h"
#define PC_SD_ROOT "/sd"
#define PC_SD_DIR "/sd/papercolor"
esp_err_t pc_sd_mount(void);
/* Must run before pc_board_quiet(), which cuts the card's power. */
void pc_sd_unmount(void);
bool pc_sd_ready(void);
