#include "board.h"
#include "buttons.h"
#include "led.h"
#include "services.h"
#include "render.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG="calendar";
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static pc_actions_t pending;
static RTC_DATA_ATTR uint32_t last_frame,retained_magic;
static RTC_DATA_ATTR int view_year,view_month;
static RTC_DATA_ATTR int64_t browse_until;
static RTC_DATA_ATTR bool last_offline;
static const uint32_t MAGIC=0x50434331;
static pc_config_t config;
static pc_weather_t weather;
static pc_holidays_t *holidays;
static uint8_t *pixels;
static bool offline=true;
static int64_t last_refresh_attempt;
static bool indoor_valid;
static int indoor_temperature_x10,indoor_humidity;
static int64_t indoor_read_at;

static void enqueue(unsigned action)
{
    portENTER_CRITICAL(&lock);
    pc_actions_add(&pending,action,esp_timer_get_time());
    portEXIT_CRITICAL(&lock);
    pc_led_attended();
}
static void read_indoor(void)
{
    esp_err_t err=pc_climate_read(&indoor_temperature_x10,&indoor_humidity);
    indoor_valid=err==ESP_OK; indoor_read_at=esp_timer_get_time();
    int t=abs(indoor_temperature_x10);
    if (indoor_valid) ESP_LOGI(TAG,"indoor %s%d.%d C %d%%",indoor_temperature_x10<0?"-":"",t/10,t%10,indoor_humidity);
    else ESP_LOGW(TAG,"SHT40 read failed: %s",esp_err_to_name(err));
}
static void button_task(void *arg)
{
    (void)arg; pc_buttons_t state={0};
    for (;;) {
        unsigned down=(!gpio_get_level(PC_KEY_A)) | ((!gpio_get_level(PC_KEY_B))<<1) | ((!gpio_get_level(PC_KEY_C))<<2);
        unsigned actions=pc_buttons_update(&state,down,esp_timer_get_time()/1000);
        if (actions) enqueue(actions);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static bool current_date(pc_date_t *date)
{
    time_t now=time(NULL); struct tm t; localtime_r(&now,&t);
    *date=(pc_date_t){t.tm_year+1900,t.tm_mon+1,t.tm_mday};
    return date->year>=2000 && date->year<=2099;
}
static void draw(bool setup)
{
    pc_screen_t screen={0};
    screen.time_valid=current_date(&screen.today);
    screen.view_year=view_year; screen.view_month=view_month;
    screen.weather=weather; screen.offline=offline; screen.configuring=setup;
    screen.battery_percent=pc_battery_percent();
    pc_led_battery(screen.battery_percent);
    if (!indoor_valid || esp_timer_get_time()-indoor_read_at>600000000) read_indoor();
    screen.indoor_valid=indoor_valid;
    screen.indoor_temperature=indoor_temperature_x10>=0?(indoor_temperature_x10+5)/10:(indoor_temperature_x10-5)/10;
    screen.indoor_humidity=indoor_humidity;
    snprintf(screen.city,sizeof(screen.city),"%s",config.city);
    if (weather.valid) {
        time_t epoch=weather.fetched; struct tm t; localtime_r(&epoch,&t);
        strftime(screen.updated,sizeof(screen.updated),"更新 %m-%d %H:%M",&t);
    }
    pc_render(pixels,&screen,holidays);
    uint32_t hash=pc_frame_hash(pixels,PC_PIXELS);
    if (retained_magic==MAGIC && hash==last_frame) return;
    last_refresh_attempt=esp_timer_get_time();
    pc_led_state(PC_LED_REFRESH);
    esp_err_t err=pc_epd_refresh(pixels);
    pc_led_state(setup?PC_LED_SETUP:PC_LED_IDLE);
    if (err==ESP_OK) { last_frame=hash; retained_magic=MAGIC; }
    else { retained_magic=0; ESP_LOGE(TAG,"refresh failed: %s",esp_err_to_name(err)); }
}
static void update_weather(void)
{
    int64_t deadline=esp_timer_get_time()+60000000;
    offline=true;
    pc_led_state(PC_LED_NETWORK);
    if (pc_wifi_connect(&config,deadline)==ESP_OK) {
        if (pc_sync_time(deadline)) {
            esp_err_t err=pc_rtc_write(time(NULL));
            if (err!=ESP_OK) ESP_LOGW(TAG,"RTC sync failed: %s",esp_err_to_name(err));
        }
        pc_date_t d;
        if (current_date(&d)) {
            pc_weather_t fresh={0};
            esp_err_t err=pc_weather_fetch(&config,&fresh,deadline);
            if (err==ESP_OK) {
                weather=fresh; offline=false;
                err=pc_weather_save(&weather);
                if (err!=ESP_OK) ESP_LOGW(TAG,"weather cache save failed: %s",esp_err_to_name(err));
            } else ESP_LOGW(TAG,"weather update failed: %s",esp_err_to_name(err));
        }
    }
    last_offline=offline;
    pc_wifi_off();
    pc_led_state(PC_LED_IDLE);
}
static void return_today(void)
{
    pc_date_t d;
    if (current_date(&d)) { view_year=d.year; view_month=d.month; }
    else { view_year=2000; view_month=1; }
    browse_until=0;
}
static void setup_mode(void)
{
    esp_err_t err=pc_setup_start(&config);
    if (err!=ESP_OK) { ESP_LOGE(TAG,"setup start failed: %s",esp_err_to_name(err)); pc_setup_stop(); return; }
    pc_led_attended(); pc_led_state(PC_LED_SETUP);
    draw(true);
    for (;;) {
        bool exit;
        portENTER_CRITICAL(&lock);
        exit=pending.today; pending.today=false; pending.setup=false; pending.delta=0;
        portEXIT_CRITICAL(&lock);
        if (exit || pc_setup_finished() || esp_timer_get_time()-pc_setup_activity()>600000000) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    pc_setup_stop();
    pc_led_state(PC_LED_IDLE);
    pc_config_load(&config);
    pc_weather_load(&weather,config.location);
    pc_holidays_free(holidays); holidays=pc_holidays_load();
    pc_date_t date;
    if (current_date(&date)) {
        err=pc_rtc_write(time(NULL)); if (err!=ESP_OK) ESP_LOGW(TAG,"RTC write failed: %s",esp_err_to_name(err));
    }
    return_today();
}
static bool ready_actions(int *delta,bool *today,bool *setup)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    pc_actions_t actions;
    ready=pc_actions_take(&pending,esp_timer_get_time(),&actions);
    if (ready) { *delta=actions.delta; *today=actions.today; *setup=actions.setup; }
    portEXIT_CRITICAL(&lock);
    return ready;
}
static void sleep_until_next(void)
{
    time_t now=time(NULL);
    unsigned seconds=3600-(now%3600);
    if (browse_until>now && browse_until-now<seconds) seconds=browse_until-now;
    /* RTC IRQ on GPIO7 plus user keys. ESP timer is a fallback if RTC I2C fails. */
    esp_err_t err=pc_rtc_arm(seconds);
    uint64_t mask=PC_KEY_MASK;
    if (err==ESP_OK) mask|=1ULL<<7;
    else ESP_LOGW(TAG,"RTC arm failed; using ESP timer: %s",esp_err_to_name(err));
    err=pc_board_quiet(); if (err!=ESP_OK) ESP_LOGW(TAG,"peripheral shutdown: %s",esp_err_to_name(err));
    esp_sleep_enable_timer_wakeup((uint64_t)seconds*1000000);
    esp_sleep_enable_ext1_wakeup_io(mask,ESP_EXT1_WAKEUP_ANY_LOW);
    for (int i=0;i<4;i++) {
        int pin=i==0?PC_KEY_A:i==1?PC_KEY_B:i==2?PC_KEY_C:7;
        rtc_gpio_pullup_en(pin); rtc_gpio_pulldown_dis(pin);
    }
    /* Don't sleep on a held key, otherwise EXT1 immediately wakes again. */
    while (!gpio_get_level(PC_KEY_A)||!gpio_get_level(PC_KEY_B)||!gpio_get_level(PC_KEY_C)) vTaskDelay(pdMS_TO_TICKS(20));
    portENTER_CRITICAL(&lock);
    bool has_pending=pending.today||pending.setup||pending.delta;
    portEXIT_CRITICAL(&lock);
    if (!has_pending) { ESP_LOGI(TAG,"sleep %u seconds",seconds); pc_led_shutdown(); esp_deep_sleep_start(); }
}
void app_main(void)
{
    setenv("TZ","CST-8",1); tzset();
    if (pc_storage_init()!=ESP_OK) { ESP_LOGE(TAG,"NVS init failed; preserve storage and repair via serial"); return; }
    if (pc_board_init()!=ESP_OK) { ESP_LOGE(TAG,"board init failed"); return; }
    /* Measure before Wi-Fi, LEDs and the panel warm the board. */
    read_indoor();
    time_t rtc_time;
    if (pc_rtc_read(&rtc_time)==ESP_OK) { struct timeval tv={.tv_sec=rtc_time}; settimeofday(&tv,NULL); }
    pc_config_load(&config); pc_weather_load(&weather,config.location); holidays=pc_holidays_load();
    pixels=heap_caps_malloc(PC_PIXELS,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!pixels) { ESP_LOGE(TAG,"PSRAM framebuffer allocation failed"); return; }
    if (pc_wifi_init()!=ESP_OK) { ESP_LOGE(TAG,"Wi-Fi init failed"); return; }
    uint32_t causes=esp_sleep_get_wakeup_causes();
    bool power_boot=(causes&(1u<<ESP_SLEEP_WAKEUP_UNDEFINED))!=0;
    uint64_t wake=(causes&(1u<<ESP_SLEEP_WAKEUP_EXT1))?esp_sleep_get_ext1_wakeup_status():0;
    bool button_wake=(wake&PC_KEY_MASK)!=0;
    esp_err_t led_err=pc_led_init(power_boot||button_wake);
    if (led_err!=ESP_OK) ESP_LOGW(TAG,"RGB LED init failed: %s",esp_err_to_name(led_err));
    pc_led_battery(pc_battery_percent());
    if (power_boot) pc_led_boot();
    offline=retained_magic==MAGIC?last_offline:true;
    if (retained_magic!=MAGIC || !browse_until || time(NULL)>=browse_until) return_today();
    if (button_wake) {
        if ((wake&(1ULL<<PC_KEY_A)) && gpio_get_level(PC_KEY_A)) enqueue(PC_ACTION_PREVIOUS);
        if ((wake&(1ULL<<PC_KEY_B)) && gpio_get_level(PC_KEY_B)) enqueue(PC_ACTION_NEXT);
        if ((wake&(1ULL<<PC_KEY_C)) && gpio_get_level(PC_KEY_C)) enqueue(PC_ACTION_TODAY);
    }
    if (xTaskCreate(button_task,"buttons",3072,NULL,5,NULL)!=pdPASS) { ESP_LOGE(TAG,"button task failed"); return; }
#ifdef CONFIG_PC_PANEL_TEST
    pc_render_test(pixels);
    ESP_LOGI(TAG,"panel test: %s",esp_err_to_name(pc_epd_refresh(pixels)));
    return;
#endif
    if (!pc_config_valid(&config)) { setup_mode(); if (pc_config_valid(&config)) update_weather(); draw(false); }
    else if (!button_wake) {
        /* A five-minute browse timeout should not trigger another weather fetch. */
        if (!weather.valid || time(NULL)-weather.fetched>=3500 || power_boot) update_weather();
        if (!browse_until || time(NULL)>=browse_until) return_today();
        draw(false);
    }
    int64_t awake_until=esp_timer_get_time()+2000000;
    for (;;) {
        int delta=0; bool today=false,setup=false;
        if (ready_actions(&delta,&today,&setup)) {
            if (setup) { setup_mode(); if (pc_config_valid(&config)) update_weather(); }
            else if (today) { return_today(); if (pc_config_valid(&config)) update_weather(); return_today(); }
            if (!setup && delta) { pc_shift_month(&view_year,&view_month,delta); browse_until=time(NULL)+300; }
            draw(false); awake_until=esp_timer_get_time()+1500000;
        }
        if (esp_timer_get_time()>awake_until && esp_timer_get_time()-last_refresh_attempt>1000000) sleep_until_next();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
