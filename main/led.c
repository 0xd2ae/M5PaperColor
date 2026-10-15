/* Two WS2812-class LEDs on G21 (GRB, 800 kHz), powered by the M5PM1 LDO.
 * Pin, count and timing follow M5Unified; see docs/REFERENCES.md. */
#include "led.h"
#include "board.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#define LED_GPIO 21
#define LED_COUNT 2
#define LOW_BATTERY 20
#define CRITICAL_BATTERY 5

static const char *TAG="led";
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static SemaphoreHandle_t tx_lock;
static volatile pc_led_state_t state;
static volatile int battery=-1;
static volatile bool attended,stopped,ready;
static volatile int64_t boot_until;
static uint8_t shown[LED_COUNT][3];

/* 10 MHz ticks: 0.3/0.9 us bits, 280 us latch. */
static const rmt_symbol_word_t bit0={.level0=1,.duration0=3,.level1=0,.duration1=9};
static const rmt_symbol_word_t bit1={.level0=1,.duration0=9,.level1=0,.duration1=3};
static const rmt_symbol_word_t latch={.level0=0,.duration0=1400,.level1=0,.duration1=1400};

static size_t encode(const void *data,size_t size,size_t written,size_t free,rmt_symbol_word_t *symbols,bool *done,void *arg)
{
    (void)arg;
    if (free<8) return 0;
    size_t index=written/8;
    if (index>=size) { symbols[0]=latch; *done=true; return 1; }
    uint8_t byte=((const uint8_t *)data)[index];
    for (int i=0;i<8;i++) symbols[i]=byte&(0x80>>i)?bit1:bit0;
    return 8;
}
static esp_err_t send(const uint8_t rgb[LED_COUNT][3])
{
    uint8_t grb[LED_COUNT*3];
    for (int i=0;i<LED_COUNT;i++) { grb[i*3]=rgb[i][1]; grb[i*3+1]=rgb[i][0]; grb[i*3+2]=rgb[i][2]; }
    rmt_transmit_config_t config={0};
    esp_err_t err=rmt_transmit(channel,encoder,grb,sizeof(grb),&config);
    if (err==ESP_OK) err=rmt_tx_wait_all_done(channel,100);
    if (err==ESP_OK) memcpy(shown,rgb,sizeof(shown));
    return err;
}
static void compose(int64_t ms,uint8_t out[LED_COUNT][3])
{
    static const uint8_t colors[][3]={[PC_LED_IDLE]={0,0,0},[PC_LED_NETWORK]={0,0,40},
        [PC_LED_REFRESH]={36,24,0},[PC_LED_SETUP]={24,0,36}};
    static const uint8_t green[3]={0,36,0},red[3]={48,0,0};
    memset(out,0,LED_COUNT*3);
    int level=battery;
    if (level>=0 && level<=CRITICAL_BATTERY) {
        if (ms%500<250) for (int i=0;i<LED_COUNT;i++) memcpy(out[i],red,3);
        return;
    }
    const uint8_t *color=NULL;
    if (attended) {
        if (ms<boot_until) color=green;
        else if (state!=PC_LED_SETUP || ms%1000<500) color=colors[state];
    }
    if (color) for (int i=0;i<LED_COUNT;i++) memcpy(out[i],color,3);
    if (level>=0 && level<=LOW_BATTERY && ms%2000<200) memcpy(out[LED_COUNT-1],red,3);
}
static void led_task(void *arg)
{
    (void)arg; bool warned=false;
    for (;;) {
        uint8_t next[LED_COUNT][3];
        compose(esp_timer_get_time()/1000,next);
        xSemaphoreTake(tx_lock,portMAX_DELAY);
        if (!stopped && memcmp(next,shown,sizeof(shown))) {
            esp_err_t err=send(next);
            if (err!=ESP_OK && !warned) { ESP_LOGW(TAG,"write failed: %s",esp_err_to_name(err)); warned=true; }
        }
        xSemaphoreGive(tx_lock);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
static esp_err_t start(void)
{
    rmt_tx_channel_config_t tx={.clk_src=RMT_CLK_SRC_DEFAULT,.gpio_num=LED_GPIO,.mem_block_symbols=64,
        .resolution_hz=10000000,.trans_queue_depth=1};
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx,&channel),TAG,"rmt channel");
    rmt_simple_encoder_config_t config={.callback=encode};
    ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&config,&encoder),TAG,"rmt encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(channel),TAG,"rmt enable");
    ESP_RETURN_ON_ERROR(pc_board_led_power(true),TAG,"power");
    vTaskDelay(pdMS_TO_TICKS(10));
    /* Freshly powered LEDs may latch a random colour. */
    static const uint8_t off[LED_COUNT][3];
    ESP_RETURN_ON_ERROR(send(off),TAG,"clear");
    tx_lock=xSemaphoreCreateMutex();
    if (!tx_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(led_task,"led",2560,NULL,3,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
esp_err_t pc_led_init(bool is_attended)
{
    attended=is_attended;
    esp_err_t err=start();
    if (err==ESP_OK) ready=true;
    else pc_board_led_power(false);
    return err;
}
void pc_led_attended(void) { attended=true; }
void pc_led_boot(void) { boot_until=esp_timer_get_time()/1000+1500; }
void pc_led_state(pc_led_state_t next) { state=next; }
void pc_led_battery(int percent) { battery=percent; }
void pc_led_shutdown(void)
{
    if (ready) {
        xSemaphoreTake(tx_lock,portMAX_DELAY);
        stopped=true;
        uint8_t rgb[LED_COUNT][3];
        if (attended && !(battery>=0 && battery<=CRITICAL_BATTERY)) {
            for (int level=24;level>0;level-=3) { memset(rgb,level,sizeof(rgb)); send(rgb); vTaskDelay(pdMS_TO_TICKS(50)); }
        }
        memset(rgb,0,sizeof(rgb));
        esp_err_t err=send(rgb);
        if (err!=ESP_OK) ESP_LOGW(TAG,"clear failed: %s",esp_err_to_name(err));
        xSemaphoreGive(tx_lock);
    }
    esp_err_t err=pc_board_led_power(false);
    if (err!=ESP_OK) ESP_LOGW(TAG,"power off failed: %s",esp_err_to_name(err));
}
