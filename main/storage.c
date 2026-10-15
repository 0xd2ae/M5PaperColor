#include "services.h"
#include "sdcard.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
extern const uint8_t holiday_start[] __asm__("_binary_holidays_json_start");
extern const uint8_t holiday_end[] __asm__("_binary_holidays_json_end");
static const char *TAG="storage";
static nvs_handle_t store;
#define CONFIG_PATH PC_SD_DIR "/config.json"
#define HOLIDAYS_PATH PC_SD_DIR "/holidays.json"
#define ANNIVERSARIES_PATH PC_SD_DIR "/anniversaries.json"

esp_err_t pc_storage_init(void)
{
    /* Never erase NVS automatically on initialization failures. */
    esp_err_t err=nvs_flash_init();
    return err==ESP_OK ? nvs_open("papercalendar",NVS_READWRITE,&store) : err;
}
bool pc_storage_card(void) { return pc_sd_ready(); }
char *pc_file_read(const char *path,size_t maximum,size_t *length)
{
    char tmp[160]; snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(path,"rb");
    /* A crash between unlink and rename leaves only the complete .tmp file. */
    if (!f) f=fopen(tmp,"rb");
    if (!f) return NULL;
    char *body=NULL; size_t used=0;
    if (fseek(f,0,SEEK_END)==0) {
        long size=ftell(f);
        if (size>=0 && (size_t)size<=maximum && fseek(f,0,SEEK_SET)==0 && (body=malloc(size+1))) {
            used=fread(body,1,size,f);
            if (used!=(size_t)size) { free(body); body=NULL; }
            else body[used]=0;
        }
    }
    fclose(f);
    if (body && length) *length=used;
    return body;
}
esp_err_t pc_file_commit(const char *tmp,const char *path)
{
    unlink(path);
    if (rename(tmp,path)) { ESP_LOGE(TAG,"rename %s: %d",path,errno); return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t pc_file_write(const char *path,const void *data,size_t length)
{
    if (!pc_sd_ready()) return ESP_ERR_INVALID_STATE;
    char tmp[160]; snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"wb");
    if (!f) return ESP_FAIL;
    bool ok=fwrite(data,1,length,f)==length && fflush(f)==0 && fsync(fileno(f))==0;
    ok=fclose(f)==0 && ok;
    if (!ok) { unlink(tmp); return ESP_FAIL; }
    return pc_file_commit(tmp,path);
}
void pc_config_load(pc_config_t *config)
{
    memset(config,0,sizeof(*config));
    char *json=pc_storage_card()?pc_file_read(CONFIG_PATH,8192,NULL):NULL;
    if (json && !pc_config_from_json(json,config)) ESP_LOGW(TAG,"config.json is not valid JSON");
    free(json);
}
esp_err_t pc_config_save(const pc_config_t *config)
{
    char *json=pc_config_to_json(config);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t err=pc_file_write(CONFIG_PATH,json,strlen(json)); free(json); return err;
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
    char *json=pc_storage_card()?pc_file_read(HOLIDAYS_PATH,PC_HOLIDAY_MAX_JSON,&n):NULL;
    if (json && n) table=pc_holidays_parse(json,n,error,sizeof(error));
    if (json && !table) ESP_LOGW(TAG,"holidays.json rejected: %s",error);
    free(json);
    if (!table) table=pc_holidays_parse((const char *)holiday_start,holiday_end-holiday_start-1,error,sizeof(error));
    if (!table) ESP_LOGE(TAG,"holiday parse failed: %s",error);
    return table;
}
esp_err_t pc_holidays_save(const char *json,size_t length) { return pc_file_write(HOLIDAYS_PATH,json,length); }
void pc_storage_migrate(void)
{
    if (!pc_storage_card()) return;
    struct stat st;
    pc_config_t old; size_t length=sizeof(old);
    if (stat(CONFIG_PATH,&st) && nvs_get_blob(store,"config_v1",&old,&length)==ESP_OK && length==sizeof(old)) {
        old.ssid[32]=0; old.password[64]=0; old.host[127]=0; old.api_key[128]=0; old.city[96]=0; old.location[32]=0;
        pc_config_t check;
        if (pc_config_save(&old)==ESP_OK) {
            pc_config_load(&check);
            if (!strcmp(check.ssid,old.ssid) && !strcmp(check.api_key,old.api_key) && !strcmp(check.password,old.password) &&
                check.coordinates==old.coordinates && nvs_erase_key(store,"config_v1")==ESP_OK && nvs_commit(store)==ESP_OK)
                ESP_LOGI(TAG,"moved NVS config to the card");
        }
    }
    if (stat(HOLIDAYS_PATH,&st) && nvs_get_blob(store,"holidays",NULL,&length)==ESP_OK && length && length<=PC_HOLIDAY_MAX_JSON) {
        char *json=malloc(length);
        if (json && nvs_get_blob(store,"holidays",json,&length)==ESP_OK && pc_holidays_save(json,length)==ESP_OK &&
            nvs_erase_key(store,"holidays")==ESP_OK && nvs_commit(store)==ESP_OK) ESP_LOGI(TAG,"moved NVS holidays to the card");
        free(json);
    }
}
void pc_anniversaries_load(pc_anniversaries_t *list)
{
    memset(list,0,sizeof(*list));
    char error[96];
    char *json=pc_storage_card()?pc_file_read(ANNIVERSARIES_PATH,PC_ANNIV_MAX_JSON,NULL):NULL;
    if (json && !pc_anniversaries_parse(json,list,error,sizeof(error))) ESP_LOGW(TAG,"anniversaries.json rejected: %s",error);
    free(json);
}
esp_err_t pc_anniversaries_save(const pc_anniversaries_t *list)
{
    char *json=pc_anniversaries_json(list);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t err=pc_file_write(ANNIVERSARIES_PATH,json,strlen(json)); free(json); return err;
}
bool pc_image_path(const char *id,const char *kind,char *out,size_t capacity)
{
    if (!pc_anniversary_id_valid(id) || (strcmp(kind,"e6") && strcmp(kind,"jpg") && strcmp(kind,"json"))) return false;
    return snprintf(out,capacity,PC_SD_DIR "/images/%s.%s",id,kind)<(int)capacity;
}
esp_err_t pc_image_read(const char *id,uint8_t *packed)
{
    char path[96];
    if (!pc_storage_card() || !pc_image_path(id,"e6",path,sizeof(path))) return ESP_ERR_INVALID_ARG;
    FILE *f=fopen(path,"rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    size_t n=fread(packed,1,PC_PACKED_BYTES,f); fclose(f);
    if (n!=PC_PACKED_BYTES) return ESP_ERR_INVALID_SIZE;
    return pc_packed_valid(packed) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
void pc_image_remove(const char *id)
{
    static const char *kinds[]={"e6","jpg","json"};
    char path[96];
    for (int i=0;i<3;i++) if (pc_image_path(id,kinds[i],path,sizeof(path))) unlink(path);
}
