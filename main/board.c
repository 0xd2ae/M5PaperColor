/* Native IDF implementation. ED2208 commands reference M5GFX (MIT / BSD-2);
 * RX8130 and M5PM1 register sequences reference M5Stack MIT code.
 * See licenses/ and docs/REFERENCES.md. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG="board";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t pm,rtc,sht,codec;
static spi_device_handle_t panel;
static bool panel_ready;
static esp_err_t read_reg(i2c_master_dev_handle_t dev,uint8_t reg,uint8_t *data,size_t count)
{
    return i2c_master_transmit_receive(dev,&reg,1,data,count,500);
}
static esp_err_t write_regs(i2c_master_dev_handle_t dev,uint8_t reg,const uint8_t *data,size_t count)
{
    uint8_t bytes[16];
    if (count>15) return ESP_ERR_INVALID_SIZE;
    bytes[0]=reg; memcpy(bytes+1,data,count);
    return i2c_master_transmit(dev,bytes,count+1,500);
}
static esp_err_t write_reg(i2c_master_dev_handle_t dev,uint8_t reg,uint8_t value)
{
    return write_regs(dev,reg,&value,1);
}
static esp_err_t update(i2c_master_dev_handle_t dev,uint8_t reg,uint8_t clear,uint8_t set)
{
    uint8_t value;
    esp_err_t err=read_reg(dev,reg,&value,1);
    return err==ESP_OK ? write_reg(dev,reg,(value&~clear)|set) : err;
}
esp_err_t pc_board_init(void)
{
    rtc_gpio_deinit(PC_KEY_A); rtc_gpio_deinit(PC_KEY_B); rtc_gpio_deinit(PC_KEY_C); rtc_gpio_deinit(7);
    gpio_config_t keys={.pin_bit_mask=PC_KEY_MASK,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&keys),TAG,"keys");
    /* Recover a bus left mid-byte by a reset, before installing the driver. */
    gpio_set_direction(2,GPIO_MODE_INPUT_OUTPUT_OD); gpio_set_direction(3,GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(3,1); gpio_set_level(2,1);
    for (int i=0;i<9 && !gpio_get_level(3);i++) {
        gpio_set_level(2,0); esp_rom_delay_us(10); gpio_set_level(2,1); esp_rom_delay_us(10);
    }
    gpio_set_level(3,0); esp_rom_delay_us(10); gpio_set_level(2,1); esp_rom_delay_us(10); gpio_set_level(3,1);
    i2c_master_bus_config_t cfg={.i2c_port=I2C_NUM_0,.sda_io_num=3,.scl_io_num=2,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg,&bus),TAG,"i2c bus");
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x6e,.scl_speed_hz=100000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus,&dev,&pm),TAG,"pm1");
    dev.device_address=0x32;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus,&dev,&rtc),TAG,"rtc");
    dev.device_address=0x44;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus,&dev,&sht),TAG,"sht40");
    dev.device_address=0x18;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus,&dev,&codec),TAG,"es8311");
    ESP_RETURN_ON_ERROR(write_reg(pm,0x09,0),TAG,"pm sleep");
    ESP_RETURN_ON_ERROR(write_reg(pm,0x0a,0),TAG,"pm watchdog");
    /* Main DC/DC remains on; charging on; RGB LDO and Grove boost off. */
    ESP_RETURN_ON_ERROR(update(pm,0x06,0x1c,0x03),TAG,"pm power");
    /* PYG0 EPD power, PYG3 SD power, PYG4 SD detect pull-up: outputs. PYG1 card detect: input. */
    ESP_RETURN_ON_ERROR(update(pm,0x16,0xcf,0),TAG,"epd/sd gpio function");
    ESP_RETURN_ON_ERROR(update(pm,0x17,0x03,0),TAG,"sd detect function");
    ESP_RETURN_ON_ERROR(update(pm,0x10,0x02,0x19),TAG,"epd/sd direction");
    ESP_RETURN_ON_ERROR(update(pm,0x13,0x19,0),TAG,"push pull");
    ESP_RETURN_ON_ERROR(update(pm,0x11,0x19,0),TAG,"epd/sd off");
    ESP_RETURN_ON_ERROR(update(rtc,0x1f,0,0x30),TAG,"rtc battery");
    ESP_RETURN_ON_ERROR(write_reg(rtc,0x30,0),TAG,"rtc setup");
    ESP_RETURN_ON_ERROR(write_reg(rtc,0x1e,0),TAG,"rtc IRQ disable");
    ESP_RETURN_ON_ERROR(write_reg(rtc,0x1d,0xa7),TAG,"rtc IRQ clear");
    gpio_config_t outputs={.pin_bit_mask=(1ULL<<12)|(1ULL<<43)|(1ULL<<44)|(1ULL<<45)|(1ULL<<46)|(1ULL<<47),.mode=GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&outputs),TAG,"outputs");
    gpio_set_level(44,1); gpio_set_level(47,1); gpio_set_level(45,0); gpio_set_level(46,0); gpio_set_level(12,0);
    gpio_set_direction(11,GPIO_MODE_INPUT); gpio_pullup_en(11);
    gpio_set_direction(7,GPIO_MODE_INPUT); gpio_pullup_en(7);
    spi_bus_config_t spi={.mosi_io_num=13,.miso_io_num=14,.sclk_io_num=15,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=4096};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&spi,SPI_DMA_CH_AUTO),TAG,"spi");
    spi_device_interface_config_t dc={.clock_speed_hz=4000000,.mode=0,.spics_io_num=-1,.queue_size=1};
    return spi_bus_add_device(SPI2_HOST,&dc,&panel);
}
static esp_err_t send_bytes(bool data,const uint8_t *bytes,size_t count)
{
    gpio_set_level(43,data);
    spi_transaction_t t={.length=count*8,.tx_buffer=bytes};
    return spi_device_polling_transmit(panel,&t);
}
/* The panel CS (G44) is manual, so hold the shared bus while it is low;
 * otherwise SD transactions would clock into the panel. */
static esp_err_t command(uint8_t cmd,const uint8_t *data,size_t count)
{
    esp_err_t err=spi_device_acquire_bus(panel,portMAX_DELAY);
    if (err!=ESP_OK) return err;
    gpio_set_level(44,0);
    err=send_bytes(false,&cmd,1);
    if (err==ESP_OK && count) err=send_bytes(true,data,count);
    gpio_set_level(44,1);
    spi_device_release_bus(panel);
    return err;
}
static esp_err_t wait_ready(unsigned timeout_ms)
{
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    /* Allow the controller to assert BUSY after a command. */
    vTaskDelay(pdMS_TO_TICKS(20));
    while (!gpio_get_level(11)) {
        if (esp_timer_get_time()>=deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}
static esp_err_t panel_init(void)
{
    static const uint8_t init[]={
        0xaa,6,0x49,0x55,0x20,0x08,0x09,0x18,
        0x01,1,0x3f, 0x00,2,0x5f,0x69, 0x05,4,0x40,0x1f,0x1f,0x2c,
        0x08,4,0x6f,0x1f,0x1f,0x22, 0x06,4,0x6f,0x1f,0x17,0x17,
        0x03,4,0x03,0x54,0x00,0x44, 0x60,2,0x02,0x00, 0x30,1,0x08,
        0x50,1,0x3f, 0xe3,1,0x2f, 0x84,1,0x01};
    ESP_RETURN_ON_ERROR(update(pm,0x11,0,1),TAG,"epd power");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(12,1); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(12,0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(12,1); vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(wait_ready(5000),TAG,"reset BUSY");
    for (size_t i=0;i<sizeof(init);) {
        uint8_t cmd=init[i++],count=init[i++];
        ESP_RETURN_ON_ERROR(command(cmd,init+i,count),TAG,"init command"); i+=count;
    }
    const uint8_t resolution[]={0x01,0x90,0x02,0x58};
    ESP_RETURN_ON_ERROR(command(0x61,resolution,sizeof(resolution)),TAG,"resolution");
    panel_ready=true;
    return ESP_OK;
}
esp_err_t pc_epd_refresh(const uint8_t *pixels)
{
    static const uint8_t palette[]={0,1,3,2,5,6};
    if (!panel_ready) ESP_RETURN_ON_ERROR(panel_init(),TAG,"panel init");
    uint8_t *chunk=heap_caps_malloc(4000,MALLOC_CAP_DMA);
    if (!chunk) return ESP_ERR_NO_MEM;
    uint8_t cmd=0x10;
    esp_err_t err=spi_device_acquire_bus(panel,portMAX_DELAY);
    if (err!=ESP_OK) { free(chunk); return err; }
    gpio_set_level(44,0);
    err=send_bytes(false,&cmd,1);
    for (size_t offset=0;err==ESP_OK && offset<PC_PIXELS;offset+=8000) {
        size_t count=PC_PIXELS-offset; if (count>8000) count=8000;
        for (size_t i=0;i<count;i+=2) {
            uint8_t a=pixels[offset+i],b=pixels[offset+i+1];
            if (a>=6 || b>=6) { err=ESP_ERR_INVALID_ARG; break; }
            chunk[i/2]=(palette[a]<<4)|palette[b];
        }
        if (err==ESP_OK) err=send_bytes(true,chunk,count/2);
    }
    gpio_set_level(44,1); spi_device_release_bus(panel); free(chunk);
    const uint8_t booster[]={0x6f,0x1f,0x17,0x27},zero=0;
    if (err==ESP_OK) err=command(0x04,NULL,0);
    if (err==ESP_OK) err=wait_ready(10000);
    if (err==ESP_OK) err=command(0x06,booster,sizeof(booster));
    vTaskDelay(pdMS_TO_TICKS(200));
    if (err==ESP_OK) err=command(0x12,&zero,1);
    if (err==ESP_OK) err=wait_ready(60000);
    if (err==ESP_OK) err=command(0x02,&zero,1);
    if (err==ESP_OK) err=wait_ready(10000);
    if (err!=ESP_OK) panel_ready=false;
    return err;
}
esp_err_t pc_board_quiet(void)
{
    esp_err_t err=ESP_OK;
    if (panel_ready) { uint8_t key=0xa5; err=command(0x07,&key,1); }
    panel_ready=false;
    gpio_set_level(12,0); gpio_set_level(45,0); gpio_set_level(46,0);
    /* Don't back-power the unpowered card through its CS line. */
    gpio_reset_pin(47); gpio_set_direction(47,GPIO_MODE_OUTPUT); gpio_set_level(47,0);
    esp_err_t power=update(pm,0x11,0x19,0);
    return err==ESP_OK ? power : err;
}
esp_err_t pc_board_sd_power(bool on)
{
    if (!on) return update(pm,0x11,0x18,0);
    gpio_set_level(47,1);
    return update(pm,0x11,0,0x18);
}
esp_err_t pc_board_sd_detect(bool *low)
{
    uint8_t value;
    ESP_RETURN_ON_ERROR(read_reg(pm,0x12,&value,1),TAG,"pm gpio in");
    *low=!(value&0x02);
    return ESP_OK;
}
static bool from_bcd(uint8_t value,int *out)
{
    if ((value&15)>9 || (value>>4)>9) return false;
    *out=(value>>4)*10+(value&15); return true;
}
static uint8_t to_bcd(int value) { return ((value/10)<<4)|(value%10); }
esp_err_t pc_rtc_read(time_t *epoch)
{
    uint8_t flags,bytes[7];
    ESP_RETURN_ON_ERROR(read_reg(rtc,0x1d,&flags,1),TAG,"rtc flags");
    if (flags&0x02) return ESP_ERR_INVALID_STATE; /* VLF: oscillator stopped */
    ESP_RETURN_ON_ERROR(read_reg(rtc,0x10,bytes,7),TAG,"rtc time");
    int sec,min,hour,day,month,year;
    if (!from_bcd(bytes[0]&0x7f,&sec)||!from_bcd(bytes[1]&0x7f,&min)||!from_bcd(bytes[2]&0x3f,&hour)||
        !from_bcd(bytes[4]&0x3f,&day)||!from_bcd(bytes[5]&0x1f,&month)||!from_bcd(bytes[6],&year)||
        sec>59||min>59||hour>23||(bytes[3]&0x80)||!bytes[3]||(bytes[3]&(bytes[3]-1))||!pc_date_valid((pc_date_t){2000+year,month,day})) return ESP_ERR_INVALID_RESPONSE;
    struct tm tm={.tm_year=year+100,.tm_mon=month-1,.tm_mday=day,.tm_hour=hour,.tm_min=min,.tm_sec=sec,.tm_isdst=-1};
    *epoch=mktime(&tm); return ESP_OK;
}
esp_err_t pc_rtc_write(time_t epoch)
{
    struct tm t; localtime_r(&epoch,&t);
    if (t.tm_year<100 || t.tm_year>199) return ESP_ERR_INVALID_ARG;
    uint8_t bytes[]={to_bcd(t.tm_sec),to_bcd(t.tm_min),to_bcd(t.tm_hour),1u<<t.tm_wday,
                     to_bcd(t.tm_mday),to_bcd(t.tm_mon+1),to_bcd(t.tm_year-100)};
    ESP_RETURN_ON_ERROR(write_regs(rtc,0x10,bytes,7),TAG,"rtc set");
    return write_reg(rtc,0x1d,0xa5); /* clear VLF, TF and AF (W0C) */
}
esp_err_t pc_rtc_arm(unsigned seconds)
{
    if (!seconds || seconds>65535) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(update(rtc,0x1c,0x17,0x02),TAG,"rtc timer stop");
    ESP_RETURN_ON_ERROR(write_reg(rtc,0x1d,0xaf),TAG,"rtc timer flag");
    uint8_t count[]={seconds&255,seconds>>8};
    ESP_RETURN_ON_ERROR(write_regs(rtc,0x1a,count,2),TAG,"rtc timer count");
    uint8_t check[2];
    ESP_RETURN_ON_ERROR(read_reg(rtc,0x1a,check,2),TAG,"rtc timer verify");
    if (memcmp(check,count,2)) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(update(rtc,0x1e,0,0x10),TAG,"rtc timer IRQ");
    return update(rtc,0x1c,0,0x10);
}
esp_err_t pc_board_led_power(bool on)
{
    return update(pm,0x06,on?0:0x04,on?0x04:0);
}
static uint8_t sht_crc(const uint8_t *data)
{
    uint8_t crc=0xff;
    for (int i=0;i<2;i++) {
        crc^=data[i];
        for (int bit=0;bit<8;bit++) crc=crc&0x80?(crc<<1)^0x31:crc<<1;
    }
    return crc;
}
esp_err_t pc_climate_read(int *temperature_x10,int *humidity)
{
    uint8_t cmd=0xfd,bytes[6];
    ESP_RETURN_ON_ERROR(i2c_master_transmit(sht,&cmd,1,500),TAG,"sht40 measure");
    /* High-precision measurement takes up to 8.3 ms. */
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(i2c_master_receive(sht,bytes,6,500),TAG,"sht40 read");
    if (sht_crc(bytes)!=bytes[2] || sht_crc(bytes+3)!=bytes[5]) return ESP_ERR_INVALID_CRC;
    int t=(int)(17500LL*((bytes[0]<<8)|bytes[1])/65535)-4500;
    int h=(int)(12500LL*((bytes[3]<<8)|bytes[4])/65535)-600;
    h=h<0?0:h>10000?10000:h;
    *temperature_x10=t>=0?(t+5)/10:(t-5)/10;
    *humidity=(h+50)/100;
    return ESP_OK;
}
int pc_battery_percent(void)
{
    uint8_t v[2];
    if (read_reg(pm,0x22,v,2)!=ESP_OK) return -1;
    int mv=v[0]|(v[1]<<8),percent=(mv-3300)*100/900;
    return percent<0 ? 0 : percent>100 ? 100 : percent;
}
esp_err_t pc_board_codec_write(uint8_t reg,uint8_t value)
{
    return codec ? write_reg(codec,reg,value) : ESP_ERR_INVALID_STATE;
}
void pc_board_audio_power(bool codec_on,bool spk_on)
{
    /* G45 powers the codec; G46 is the AW8737A enable. Drop the amp first so codec power-down does not pop. */
    if (!spk_on) gpio_set_level(46,0);
    gpio_set_level(45,codec_on);
    if (spk_on) gpio_set_level(46,1);
}
