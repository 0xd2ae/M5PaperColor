#include "board.h"
#include "buttons.h"
#include "led.h"
#include "services.h"
#include "render.h"
#include "sdcard.h"
#include "fonts.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG="calendar";
enum { PAGE_CALENDAR, PAGE_ANNIVERSARY };
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static pc_actions_t pending;
static RTC_DATA_ATTR uint32_t last_frame,retained_magic;
static RTC_DATA_ATTR int view_year,view_month;
static RTC_DATA_ATTR int64_t browse_until;
static RTC_DATA_ATTR bool last_offline;
/* auto_page: the anniversary page was chosen because today is an anniversary, not by the user. */
static RTC_DATA_ATTR int page,anniv_index,dismissed_day;
static RTC_DATA_ATTR bool auto_page;
static const uint32_t MAGIC=0x50434331;
static pc_config_t config;
static pc_weather_t weather;
static pc_holidays_t *holidays;
static pc_anniversaries_t anniversaries;
static uint8_t *pixels,*packed;
/* Guards config, weather, holidays, anniversaries, packed, indoor readings and the card font cache:
 * the setup server renders web previews from its own task while this one refreshes the panel. */
static SemaphoreHandle_t state_lock;
static int battery;
static bool offline=true,card;
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
static void card_up(void)
{
    card=pc_sd_mount()==ESP_OK;
    pc_fonts_open(card?PC_SD_DIR "/fonts":NULL);
}
static void card_down(void)
{
    pc_fonts_open(NULL); pc_sd_unmount();
}
static bool config_usable(void) { return pc_config_valid(&config) || pc_config_resolvable(&config); }
static void refresh(bool setup)
{
    uint32_t hash=pc_frame_hash(pixels,PC_PIXELS);
    if (retained_magic==MAGIC && hash==last_frame) return;
    last_refresh_attempt=esp_timer_get_time();
    pc_led_state(PC_LED_REFRESH);
    esp_err_t err=pc_epd_refresh(pixels);
    pc_led_state(setup?PC_LED_SETUP:PC_LED_IDLE);
    if (err==ESP_OK) { last_frame=hash; retained_magic=MAGIC; }
    else { retained_magic=0; ESP_LOGE(TAG,"refresh failed: %s",esp_err_to_name(err)); }
}
static void lock_state(void) { xSemaphoreTake(state_lock,portMAX_DELAY); }
static void unlock_state(void) { xSemaphoreGive(state_lock); }
static void update_battery(void) { battery=pc_battery_percent(); pc_led_battery(battery); }
/* Caller holds state_lock. item may be a draft that is not in the list. Returns the image to draw. */
static const uint8_t *anniversary_screen(pc_anniv_screen_t *s,const pc_anniversary_t *item,int index,int count,
                                         const uint8_t *image)
{
    *s=(pc_anniv_screen_t){0};
    s->time_valid=current_date(&s->today); s->card=card; s->count=count; s->battery_percent=battery;
    if (!item) return NULL;
    s->item=item; s->index=index;
    if (s->time_valid && !pc_anniversary_status(item,s->today,&s->status)) s->time_valid=false;
    if (!item->image) return NULL;
    if (image) return image;
    esp_err_t err=packed?pc_image_read(item->id,packed):ESP_ERR_NO_MEM;
    if (err==ESP_OK) return packed;
    ESP_LOGW(TAG,"image %s: %s",item->id,esp_err_to_name(err));
    return NULL;
}
/* Caller holds state_lock. */
static void calendar_screen(pc_screen_t *screen,int year,int month,bool setup)
{
    *screen=(pc_screen_t){0};
    screen->time_valid=current_date(&screen->today);
    screen->view_year=year; screen->view_month=month;
    screen->weather=weather; screen->offline=offline; screen->configuring=setup; screen->card_missing=!card;
    screen->battery_percent=battery;
    screen->indoor_valid=indoor_valid;
    screen->indoor_temperature=indoor_temperature_x10>=0?(indoor_temperature_x10+5)/10:(indoor_temperature_x10-5)/10;
    screen->indoor_humidity=indoor_humidity;
    snprintf(screen->city,sizeof(screen->city),"%s",config.city);
    if (weather.valid) {
        time_t epoch=weather.fetched; struct tm t; localtime_r(&epoch,&t);
        strftime(screen->updated,sizeof(screen->updated),"更新 %m-%d %H:%M",&t);
    }
}
static void draw_anniversary(const char *preview_id,bool setup)
{
    update_battery();
    lock_state();
    int count=anniversaries.count,index=preview_id?pc_anniversaries_find(&anniversaries,preview_id):anniv_index;
    if (index<0 || index>=count) index=0;
    pc_anniv_screen_t s;
    const uint8_t *image=anniversary_screen(&s,count?&anniversaries.items[index]:NULL,index,count,NULL);
    pc_render_anniversary(pixels,&s,image);
    unlock_state();
    refresh(setup);
}
static void draw(bool setup)
{
    if (!setup && page==PAGE_ANNIVERSARY) { draw_anniversary(NULL,false); return; }
    update_battery();
    lock_state();
    if (!indoor_valid || esp_timer_get_time()-indoor_read_at>600000000) read_indoor();
    pc_screen_t screen;
    calendar_screen(&screen,view_year,view_month,setup);
    pc_render(pixels,&screen,holidays);
    unlock_state();
    refresh(setup);
}
void pc_preview_render(const pc_preview_t *r,uint8_t *out)
{
    lock_state();
    if (r->kind==PC_PREVIEW_CALENDAR) {
        int year=r->year,month=r->month; pc_date_t d;
        if (!year) { if (current_date(&d)) { year=d.year; month=d.month; } else { year=2000; month=1; } }
        pc_screen_t screen;
        calendar_screen(&screen,year,month,false);
        pc_render(out,&screen,holidays);
    } else {
        const pc_anniversary_t *item=NULL; const uint8_t *image=NULL;
        int count=anniversaries.count,index=0;
        if (r->kind==PC_PREVIEW_DRAFT) {
            item=r->draft; image=r->draft_image;
            index=pc_anniversaries_find(&anniversaries,item->id);
            if (index<0) index=count++;
        } else if (count) {
            index=r->id[0]?pc_anniversaries_find(&anniversaries,r->id):anniv_index;
            if (index<0 || index>=count) index=0;
            item=&anniversaries.items[index];
        }
        pc_anniv_screen_t s;
        image=anniversary_screen(&s,item,index,count,image);
        pc_render_anniversary(out,&s,image);
    }
    unlock_state();
}
void pc_device_status(pc_device_status_t *st)
{
    static const int SIZES[]={18,24,36};
    lock_state();
    *st=(pc_device_status_t){0};
    pc_date_t d; st->time_valid=current_date(&d);
    st->weather_valid=weather.valid; st->weather_fetched=weather.fetched;
    st->weather_stale=weather.valid && time(NULL)-weather.fetched>3*3600;
    st->offline=offline; st->card=card; st->battery_percent=battery; st->anniversaries=anniversaries.count;
    st->anniversary_page=page==PAGE_ANNIVERSARY;
    st->celebrate_today=st->time_valid && pc_anniversaries_today(&anniversaries,d)>=0;
    for (int y=2000;holidays && y<=2099;y++) if (pc_holidays_has_year(holidays,y)) {
        if (!st->holiday_first) st->holiday_first=y;
        st->holiday_last=y;
    }
    for (int i=0;i<3;i++) st->fonts[i]=pc_fonts_available(SIZES[i]);
    unlock_state();
}
void pc_preview_reload(unsigned what)
{
    lock_state();
    if (what&PC_RELOAD_HOLIDAYS) {
        pc_holidays_t *table=pc_holidays_load();
        if (table) { pc_holidays_free(holidays); holidays=table; }
    }
    if (what&PC_RELOAD_ANNIVERSARIES) {
        pc_anniversaries_load(&anniversaries);
        if (anniv_index>=anniversaries.count) anniv_index=0;
    }
    unlock_state();
}
static void update_weather(void)
{
    if (!config_usable()) return;
    int64_t deadline=esp_timer_get_time()+60000000;
    offline=true;
    pc_led_state(PC_LED_NETWORK);
    if (pc_wifi_connect(&config,deadline)==ESP_OK) {
        if (pc_sync_time(deadline)) {
            esp_err_t err=pc_rtc_write(time(NULL));
            if (err!=ESP_OK) ESP_LOGW(TAG,"RTC sync failed: %s",esp_err_to_name(err));
        }
        if (!pc_config_valid(&config)) {
            esp_err_t err=pc_city_resolve(&config,deadline);
            if (err==ESP_OK) {
                ESP_LOGI(TAG,"resolved city %s",config.city);
                err=pc_config_save(&config);
                if (err!=ESP_OK) ESP_LOGW(TAG,"config save failed: %s",esp_err_to_name(err));
                pc_weather_load(&weather,config.location);
            } else ESP_LOGW(TAG,"city lookup failed: %s",esp_err_to_name(err));
        }
        pc_date_t d;
        if (pc_config_valid(&config) && current_date(&d)) {
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
static void leave_anniversary(void)
{
    pc_date_t d;
    if (current_date(&d) && pc_anniversaries_today(&anniversaries,d)>=0) dismissed_day=pc_day_number(d);
    page=PAGE_CALENDAR; auto_page=false;
}
static void auto_select(void)
{
    pc_date_t d;
    if (!current_date(&d)) return;
    int i=pc_anniversaries_today(&anniversaries,d);
    if (i>=0 && dismissed_day!=pc_day_number(d)) {
        if (page!=PAGE_ANNIVERSARY || auto_page) { page=PAGE_ANNIVERSARY; anniv_index=i; auto_page=true; }
    } else if (auto_page) { page=PAGE_CALENDAR; auto_page=false; }
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
        exit=pending.today; pending.today=false; pending.setup=false; pending.page=false; pending.delta=0;
        portEXIT_CRITICAL(&lock);
        if (exit || pc_setup_finished() || esp_timer_get_time()-pc_setup_activity()>600000000) break;
        char id[17];
        if (pc_setup_preview(id)) { pc_preview_reload(PC_RELOAD_ANNIVERSARIES); draw_anniversary(id,true); }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    pc_setup_stop();
    pc_led_state(PC_LED_IDLE);
    lock_state();
    pc_config_load(&config);
    pc_weather_load(&weather,config.location);
    unlock_state();
    pc_preview_reload(PC_RELOAD_HOLIDAYS|PC_RELOAD_ANNIVERSARIES);
    pc_date_t date;
    if (current_date(&date)) {
        err=pc_rtc_write(time(NULL)); if (err!=ESP_OK) ESP_LOGW(TAG,"RTC write failed: %s",esp_err_to_name(err));
    }
    return_today();
}
static bool ready_actions(pc_actions_t *actions)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready=pc_actions_take(&pending,esp_timer_get_time(),actions);
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
    bool had_card=card;
    card_down();
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
    bool has_pending=pending.today||pending.setup||pending.page||pending.delta;
    portEXIT_CRITICAL(&lock);
    if (!has_pending) { ESP_LOGI(TAG,"sleep %u seconds",seconds); pc_led_shutdown(); esp_deep_sleep_start(); }
    if (had_card) card_up();
}
void app_main(void)
{
    setenv("TZ","CST-8",1); tzset();
    if (pc_storage_init()!=ESP_OK) { ESP_LOGE(TAG,"NVS init failed; preserve storage and repair via serial"); return; }
    if (pc_board_init()!=ESP_OK) { ESP_LOGE(TAG,"board init failed"); return; }
    state_lock=xSemaphoreCreateMutex();
    if (!state_lock) { ESP_LOGE(TAG,"state lock allocation failed"); return; }
    /* Measure before Wi-Fi, LEDs and the panel warm the board. */
    read_indoor();
    time_t rtc_time;
    if (pc_rtc_read(&rtc_time)==ESP_OK) { struct timeval tv={.tv_sec=rtc_time}; settimeofday(&tv,NULL); }
    card_up();
    if (card) pc_storage_migrate();
    else ESP_LOGW(TAG,"no TF card: settings, anniversaries and card fonts unavailable");
    pc_config_load(&config); pc_weather_load(&weather,config.location); holidays=pc_holidays_load();
    pc_anniversaries_load(&anniversaries);
    if (anniv_index>=anniversaries.count) anniv_index=0;
    pixels=heap_caps_malloc(PC_PIXELS,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    packed=heap_caps_malloc(PC_PACKED_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!pixels || !packed) { ESP_LOGE(TAG,"PSRAM framebuffer allocation failed"); return; }
    if (pc_wifi_init()!=ESP_OK) { ESP_LOGE(TAG,"Wi-Fi init failed"); return; }
    uint32_t causes=esp_sleep_get_wakeup_causes();
    bool power_boot=(causes&(1u<<ESP_SLEEP_WAKEUP_UNDEFINED))!=0;
    uint64_t wake=(causes&(1u<<ESP_SLEEP_WAKEUP_EXT1))?esp_sleep_get_ext1_wakeup_status():0;
    bool button_wake=(wake&PC_KEY_MASK)!=0;
    esp_err_t led_err=pc_led_init(power_boot||button_wake);
    if (led_err!=ESP_OK) ESP_LOGW(TAG,"RGB LED init failed: %s",esp_err_to_name(led_err));
    update_battery();
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
    /* Without a card nothing can be saved, so only offer setup on power-on instead of every hourly wake. */
    if (!config_usable() && (card || power_boot)) { setup_mode(); update_weather(); draw(false); }
    else if (!button_wake) {
        auto_select();
        /* A five-minute browse timeout should not trigger another weather fetch. */
        if (page==PAGE_CALENDAR && (!weather.valid || time(NULL)-weather.fetched>=3500 || power_boot || !pc_config_valid(&config)))
            update_weather();
        if (!browse_until || time(NULL)>=browse_until) return_today();
        draw(false);
    }
    int64_t awake_until=esp_timer_get_time()+2000000;
    for (;;) {
        pc_actions_t a;
        if (ready_actions(&a)) {
            if (a.setup) { setup_mode(); update_weather(); }
            else {
                if (a.page) {
                    if (page==PAGE_ANNIVERSARY) leave_anniversary();
                    else { page=PAGE_ANNIVERSARY; auto_page=false; }
                }
                if (a.today) {
                    if (page==PAGE_ANNIVERSARY) leave_anniversary();
                    return_today(); update_weather(); return_today();
                }
                if (a.delta && page==PAGE_ANNIVERSARY) {
                    int n=anniversaries.count;
                    if (n) anniv_index=((anniv_index+a.delta)%n+n)%n;
                    auto_page=false;
                } else if (a.delta) { pc_shift_month(&view_year,&view_month,a.delta); browse_until=time(NULL)+300; }
            }
            draw(false); awake_until=esp_timer_get_time()+1500000;
        }
        if (esp_timer_get_time()>awake_until && esp_timer_get_time()-last_refresh_attempt>1000000) sleep_until_next();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
