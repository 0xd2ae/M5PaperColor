#include "services.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
extern const uint8_t holiday_start[] __asm__("_binary_holidays_json_start");
extern const uint8_t holiday_end[] __asm__("_binary_holidays_json_end");
static nvs_handle_t store;
esp_err_t pc_storage_init(void)
{
    /* Never erase credentials automatically on initialization failures. */
    esp_err_t err=nvs_flash_init();
    return err==ESP_OK ? nvs_open("papercalendar",NVS_READWRITE,&store) : err;
}
static void load_blob(const char *key,void *data,size_t length)
{
    size_t actual=length;
    if (nvs_get_blob(store,key,data,&actual)!=ESP_OK || actual!=length) memset(data,0,length);
}
static esp_err_t save_blob(const char *key,const void *data,size_t length)
{
    esp_err_t err=nvs_set_blob(store,key,data,length);
    return err==ESP_OK ? nvs_commit(store) : err;
}
void pc_config_load(pc_config_t *config)
{
    load_blob("config_v1",config,sizeof(*config));
    config->ssid[32]=0; config->password[64]=0; config->host[127]=0; config->api_key[128]=0;
    config->city[96]=0; config->location[32]=0;
}
esp_err_t pc_config_save(const pc_config_t *config) { return save_blob("config_v1",config,sizeof(*config)); }
void pc_weather_load(pc_weather_t *weather,const char *location)
{
    load_blob("weather_v1",weather,sizeof(*weather));
    weather->location[32]=0; weather->text[63]=0; weather->observation[39]=0;
    for (int i=0;i<3;i++) { weather->days[i].text[63]=0; weather->days[i].date[10]=0; }
    if (strcmp(weather->location,location)) memset(weather,0,sizeof(*weather));
}
esp_err_t pc_weather_save(const pc_weather_t *weather) { return save_blob("weather_v1",weather,sizeof(*weather)); }
pc_holidays_t *pc_holidays_load(void)
{
    size_t n=0; char error[96]; pc_holidays_t *table=NULL;
    if (nvs_get_blob(store,"holidays",NULL,&n)==ESP_OK && n && n<=PC_HOLIDAY_MAX_JSON) {
        char *json=malloc(n+1);
        if (json && nvs_get_blob(store,"holidays",json,&n)==ESP_OK) { json[n]=0; table=pc_holidays_parse(json,n,error,sizeof(error)); }
        free(json);
    }
    if (!table) table=pc_holidays_parse((const char *)holiday_start,holiday_end-holiday_start-1,error,sizeof(error));
    if (!table) ESP_LOGE("storage","holiday parse failed: %s",error);
    return table;
}
esp_err_t pc_holidays_save(const char *json,size_t length) { return save_blob("holidays",json,length); }
