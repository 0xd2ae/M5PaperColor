#include "model.h"
#include "buttons.h"
#include "render.h"
#include "compression.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_file(const char *path)
{
    FILE *file=fopen(path,"rb"); assert(file); fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    char *body=malloc(size+1); assert(body); assert(fread(body,1,size,file)==(size_t)size); fclose(file); body[size]=0; return body;
}
static void calendar_tests(void)
{
    assert(pc_month_days(2000,2)==29 && pc_month_days(2100,2)==28);
    pc_date_t date; assert(!pc_parse_date("2026-02-30",&date)); assert(!pc_parse_date("2026-1-01",&date));
    for (int n=pc_day_number((pc_date_t){2000,1,1});n<=pc_day_number((pc_date_t){2099,12,31});n++) {
        pc_date_t d=pc_date_from_day(n); assert(pc_date_valid(d) && pc_day_number(d)==n);
        pc_lunar_t lunar; assert(pc_lunar_lookup(d,&lunar)); assert(lunar.month>=1 && lunar.month<=12 && lunar.day>=1 && lunar.day<=30);
    }
    assert(pc_weekday((pc_date_t){2026,10,13})==1);
    pc_lunar_t lunar; assert(pc_lunar_lookup((pc_date_t){2026,2,17},&lunar)); assert(lunar.month==1 && lunar.day==1 && !lunar.leap);
    assert(pc_lunar_lookup((pc_date_t){2025,7,25},&lunar)); assert(lunar.month==6 && lunar.day==1 && lunar.leap);
    assert(!strcmp(pc_festival((pc_date_t){2026,2,16}),"除夕"));
    assert(!strcmp(pc_festival((pc_date_t){2026,9,25}),"中秋"));
    assert(!strcmp(pc_solar_term((pc_date_t){2026,1,5}),"小寒"));
    assert(!strcmp(pc_solar_term((pc_date_t){2026,10,8}),"寒露"));
    assert(!strcmp(pc_solar_term((pc_date_t){2026,12,22}),"冬至"));
    assert(!strcmp(pc_solar_term((pc_date_t){2026,10,9}),""));
    assert(!strcmp(pc_solar_term((pc_date_t){1999,12,22}),""));
    for (int y=2000;y<=2099;y++) for (int m=1;m<=12;m++) {
        int n=0;
        for (int day=1;day<=pc_month_days(y,m);day++) n+=*pc_solar_term((pc_date_t){y,m,day})!=0;
        assert(n==2);
    }
    int y=2026,m=1; pc_shift_month(&y,&m,-1); assert(y==2025 && m==12);
}
static pc_holidays_t *holiday_tests(void)
{
    char *json=read_file("main/generated/holidays.json"),error[100];
    pc_holidays_t *table=pc_holidays_parse(json,strlen(json),error,sizeof(error)); assert(table);
    assert(pc_holidays_has_year(table,2026) && !pc_holidays_has_year(table,2027));
    assert(pc_holidays_kind(table,(pc_date_t){2026,2,15},NULL)==PC_REST);
    assert(pc_holidays_kind(table,(pc_date_t){2026,2,23},NULL)==PC_REST);
    assert(pc_holidays_kind(table,(pc_date_t){2026,2,14},NULL)==PC_WORK);
    assert(pc_holidays_kind(table,(pc_date_t){2026,2,28},NULL)==PC_WORK);
    assert(pc_holidays_kind(table,(pc_date_t){2026,10,10},NULL)==PC_WORK);
    assert(pc_holidays_kind(table,(pc_date_t){2022,12,31},NULL)==PC_REST);
    cJSON *root=cJSON_Parse(json); assert(root);
    cJSON *years=cJSON_GetObjectItem(root,"Years"), *year=cJSON_GetObjectItem(years,"2026");
    cJSON *first=cJSON_GetArrayItem(year,0), *work=cJSON_GetObjectItem(first,"CompDays");
    cJSON_AddItemToArray(work,cJSON_CreateString("2026-01-01"));
    char *bad=cJSON_PrintUnformatted(root); assert(!pc_holidays_parse(bad,strlen(bad),error,sizeof(error))); free(bad);
    cJSON_Delete(root); free(json);
    assert(!pc_holidays_parse("{}",2,error,sizeof(error)));
    assert(!pc_holidays_parse("{",1,error,sizeof(error)));
    return table;
}
static void button_tests(void)
{
    pc_buttons_t s={0};
    assert(!pc_buttons_update(&s,1,0)); assert(!pc_buttons_update(&s,0,10)); assert(!pc_buttons_update(&s,0,50));
    assert(!pc_buttons_update(&s,1,100)); assert(!pc_buttons_update(&s,1,130));
    assert(!pc_buttons_update(&s,0,200)); assert(pc_buttons_update(&s,0,230)==PC_ACTION_PREVIOUS);
    assert(!pc_buttons_update(&s,4,500)); assert(!pc_buttons_update(&s,4,530));
    assert(pc_buttons_update(&s,4,3530)==PC_ACTION_SETUP); assert(!pc_buttons_update(&s,4,4000));
    assert(!pc_buttons_update(&s,0,4100)); assert(!pc_buttons_update(&s,0,4130));
    assert(!pc_buttons_update(&s,2,5000)); assert(!pc_buttons_update(&s,2,5030));
    assert(!pc_buttons_update(&s,0,5100)); assert(pc_buttons_update(&s,0,5130)==PC_ACTION_NEXT);
}
static void action_tests(void)
{
    pc_actions_t pending={0},out;
    pc_actions_add(&pending,PC_ACTION_NEXT,0); pc_actions_add(&pending,PC_ACTION_NEXT,200000);
    assert(!pc_actions_take(&pending,900000,&out)); assert(pc_actions_take(&pending,1000000,&out) && out.delta==2);
    pc_actions_add(&pending,PC_ACTION_PREVIOUS,1100000); pc_actions_add(&pending,PC_ACTION_NEXT,1200000);
    assert(!pc_actions_take(&pending,2100000,&out));
    pc_actions_add(&pending,PC_ACTION_NEXT,2200000); pc_actions_add(&pending,PC_ACTION_TODAY,2300000);
    assert(pc_actions_take(&pending,3100000,&out) && out.today && !out.delta);
    pc_actions_add(&pending,PC_ACTION_TODAY,3200000); pc_actions_add(&pending,PC_ACTION_NEXT,3300000);
    assert(pc_actions_take(&pending,4100000,&out) && out.today && out.delta==1);
    pc_actions_add(&pending,PC_ACTION_NEXT,4200000); pc_actions_add(&pending,PC_ACTION_SETUP,4300000);
    assert(pc_actions_take(&pending,5100000,&out) && out.setup && !out.today && !out.delta);
}
static void weather_tests(void)
{
    pc_weather_t result={0};
    const char *now="{\"condition\":{\"text\":\"晴\",\"code\":\"100\"},\"temperature\":{\"value\":23.6,\"unit\":\"°C\"}}";
    const char *daily="{\"days\":[{\"forecastStartTime\":\"2026-10-13T06:00+08:00\",\"temperatureMin\":{\"value\":18,\"unit\":\"°C\"},\"temperatureMax\":{\"value\":27,\"unit\":\"°C\"},\"daytime\":{\"condition\":{\"text\":\"晴\",\"code\":\"100\"}}},{\"forecastStartTime\":\"2026-10-14T06:00+08:00\",\"temperatureMin\":{\"value\":17,\"unit\":\"°C\"},\"temperatureMax\":{\"value\":25,\"unit\":\"°C\"},\"daytime\":{\"condition\":{\"text\":\"多云\",\"code\":\"101\"}}},{\"forecastStartTime\":\"2026-10-15T06:00+08:00\",\"temperatureMin\":{\"value\":16,\"unit\":\"°C\"},\"temperatureMax\":{\"value\":24,\"unit\":\"°C\"},\"daytime\":{\"condition\":{\"text\":\"小雨\",\"code\":\"305\"}}}]}";
    assert(pc_weather_parse(now,daily,"101280601",1791849600,&result));
    assert(result.valid && result.temperature==24 && result.days[2].icon==305);
    pc_weather_t previous=result;
    assert(!pc_weather_parse("{}",daily,"101280601",0,&result)); assert(!memcmp(&previous,&result,sizeof(result)));
    assert(!pc_weather_parse(now,"{}","101280601",0,&result));
    assert(pc_host_valid("abc.qweatherapi.com")); assert(!pc_host_valid("https://abc.qweatherapi.com"));
    assert(!pc_host_valid("abc.com/path")); assert(!pc_location_valid("a\r\nx"));
}
static void gzip_tests(void)
{
    char *raw=read_file("tests/.build/weather.gz");
    FILE *f=fopen("tests/.build/weather.gz","rb"); assert(f); fseek(f,0,SEEK_END); size_t n=ftell(f); fclose(f);
    char *body=pc_gzip_decode((uint8_t *)raw,n,60000); assert(body && !strcmp(body,"{\"temp\":24}")); free(body);
    assert(!pc_gzip_decode((uint8_t *)raw,n,4));
    raw[n-8]^=1; assert(!pc_gzip_decode((uint8_t *)raw,n,60000)); raw[n-8]^=1;
    raw[3]|=4; raw[10]=0xff; raw[11]=0xff; assert(!pc_gzip_decode((uint8_t *)raw,n,60000));
    free(raw); assert(!pc_gzip_decode((const uint8_t *)"bad",3,60000));
}
static void icon_tests(void)
{
    uint8_t *pixels=malloc(PC_PIXELS); assert(pixels);
    int codes[]={100,101,150,305,400,501,999};
    uint32_t hashes[7];
    for (int i=0;i<7;i++) {
        memset(pixels,PC_WHITE,PC_PIXELS);
        assert(pc_render_weather_icon(pixels,0,0,codes[i],80)==codes[i]);
        assert(pc_render_weather_icon(pixels,PC_WIDTH-10,PC_HEIGHT-10,codes[i],20)==codes[i]);
        hashes[i]=pc_frame_hash(pixels,PC_PIXELS);
        for (int j=0;j<i;j++) assert(hashes[j]!=hashes[i]);
    }
    memset(pixels,PC_WHITE,PC_PIXELS);
    assert(pc_render_weather_icon(pixels,0,0,123456,80)==999);
    assert(pc_render_weather_icon(pixels,0,0,-1,20)==999);
    free(pixels);
}
static void previews(pc_holidays_t *table)
{
    uint8_t *pixels=malloc(PC_PIXELS); assert(pixels);
    pc_screen_t s={.today={2026,10,13},.view_year=2026,.view_month=10,.time_valid=true,.battery_percent=76};
    strcpy(s.city,"深圳"); strcpy(s.updated,"更新 10-13 08:00");
    s.weather.valid=true; s.weather.temperature=24; s.weather.icon=100; s.weather.fetched=time(NULL);
    const char *labels[]={"晴","多云","小雨"};
    for (int i=0;i<3;i++) { strcpy(s.weather.days[i].text,labels[i]); s.weather.days[i].icon=i==0?100:i==1?101:305; s.weather.days[i].low=18-i; s.weather.days[i].high=27-i;
        snprintf(s.weather.days[i].date,sizeof(s.weather.days[i].date),"2026-10-%02d",13+i); }
    strcpy(s.weather.text,"晴");
    const char *names[]={"sunny","rain","snow","six-row","holiday","setup","offline","invalid-time","long-city","no-weather"};
    for (int i=0;i<10;i++) {
        s.weather.icon=i==1?305:i==2?400:100; strcpy(s.weather.text,i==1?"小雨":i==2?"雪":"晴");
        s.view_month=i==3?3:10; s.today=(pc_date_t){2026,10,i==4?1:13};
        s.configuring=i==5; s.offline=i==6; s.time_valid=i!=7; s.weather.valid=i!=9;
        s.indoor_valid=i!=6; s.indoor_temperature=i==2?-12:23; s.indoor_humidity=i==1||i==2?100:58;
        strcpy(s.city,i==8?"内蒙古自治区呼伦贝尔市鄂伦春自治旗":"深圳");
        pc_render(pixels,&s,table);
        char path[100]; snprintf(path,sizeof(path),"tests/.build/%s.raw",names[i]);
        FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(pixels,1,PC_PIXELS,f)==PC_PIXELS); fclose(f);
    }
    free(pixels);
}
int main(void)
{
    setenv("TZ","CST-8",1); tzset();
    calendar_tests(); pc_holidays_t *table=holiday_tests(); button_tests(); action_tests(); weather_tests(); gzip_tests(); icon_tests(); previews(table); pc_holidays_free(table);
    puts("Calendar, holidays, buttons, weather and render checks passed");
}
