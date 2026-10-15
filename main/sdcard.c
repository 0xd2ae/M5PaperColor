#include "sdcard.h"
#include "board.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>

static const char *TAG="sd";
static sdmmc_card_t *card;

esp_err_t pc_sd_mount(void)
{
    if (card) return ESP_OK;
    esp_err_t err=pc_board_sd_power(true);
    if (err!=ESP_OK) return err;
    /* SD allows up to 250 ms from power-good to ready; sdmmc init also retries. */
    vTaskDelay(pdMS_TO_TICKS(100));
    bool low=false;
    if (pc_board_sd_detect(&low)==ESP_OK) ESP_LOGI(TAG,"card detect PYG1=%s",low?"low":"high");
    sdmmc_host_t host=SDSPI_HOST_DEFAULT();
    host.slot=SPI2_HOST; host.max_freq_khz=SDMMC_FREQ_DEFAULT;
    sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id=SPI2_HOST; slot.gpio_cs=47;
    esp_vfs_fat_sdmmc_mount_config_t mount={.format_if_mount_failed=false,.max_files=4,.allocation_unit_size=16*1024};
    err=esp_vfs_fat_sdspi_mount(PC_SD_ROOT,&host,&slot,&mount,&card);
    if (err!=ESP_OK) {
        card=NULL; pc_board_sd_power(false);
        ESP_LOGW(TAG,"mount failed: %s",esp_err_to_name(err));
        return err;
    }
    mkdir(PC_SD_DIR,0775); mkdir(PC_SD_DIR "/images",0775); mkdir(PC_SD_DIR "/fonts",0775);
    ESP_LOGI(TAG,"mounted %s, %llu MB",card->cid.name,(unsigned long long)card->csd.capacity*card->csd.sector_size/(1024*1024));
    return ESP_OK;
}
void pc_sd_unmount(void)
{
    if (!card) return;
    esp_err_t err=esp_vfs_fat_sdcard_unmount(PC_SD_ROOT,card);
    if (err!=ESP_OK) ESP_LOGW(TAG,"unmount: %s",esp_err_to_name(err));
    card=NULL;
    pc_board_sd_power(false);
}
bool pc_sd_ready(void) { return card!=NULL; }
