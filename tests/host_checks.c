#include "model.h"
#include "buttons.h"
#include "render.h"
#include "anniversary.h"
#include "fonts.h"
#include "compression.h"
#include "audio.h"
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
    /* Long B toggles the anniversary page and does not also step forward on release. */
    assert(!pc_buttons_update(&s,2,6000)); assert(!pc_buttons_update(&s,2,6030));
    assert(pc_buttons_update(&s,2,9030)==PC_ACTION_PAGE); assert(!pc_buttons_update(&s,2,9500));
    assert(!pc_buttons_update(&s,0,9600)); assert(!pc_buttons_update(&s,0,9630));
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
    pc_actions_add(&pending,PC_ACTION_NEXT,6000000); pc_actions_add(&pending,PC_ACTION_PAGE,6100000);
    assert(pc_actions_take(&pending,7000000,&out) && out.page && !out.delta);
    pc_actions_add(&pending,PC_ACTION_PAGE,7100000); pc_actions_add(&pending,PC_ACTION_PAGE,7200000);
    assert(!pc_actions_take(&pending,8100000,&out));
    pc_actions_add(&pending,PC_ACTION_PAGE,8200000); pc_actions_add(&pending,PC_ACTION_SETUP,8300000);
    assert(pc_actions_take(&pending,9200000,&out) && out.setup && !out.page);
}
static pc_anniversary_t anniversary(const char *json)
{
    pc_anniversary_t a; char error[96];
    if (!pc_anniversary_parse_item(json,&a,error,sizeof(error))) { fprintf(stderr,"%s: %s\n",json,error); assert(0); }
    return a;
}
static void anniversary_tests(void)
{
    pc_anniv_status_t s; char error[96]; pc_anniversary_t a;
    a=anniversary("{\"id\":\"wed\",\"name\":\"结婚纪念日\",\"date\":\"2020-05-20\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"lunar\":null,\"image\":true,\"overlay\":\"top\",\"band\":\"red\"}");
    assert(a.image && a.overlay_top && a.band==PC_RED && a.yearly && !a.lunar && !a.melody[0]);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,10,15},&s) && s.state==PC_ANNIV_UPCOMING && s.days==217 && s.years==7);
    assert(s.next.year==2027 && s.next.month==5 && s.next.day==20 && !s.celebrate);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,5,20},&s) && s.state==PC_ANNIV_TODAY && s.days==0 && s.years==6 && s.celebrate);
    /* Feb 29 falls back to Feb 28 in common years. */
    a=anniversary("{\"id\":\"leap\",\"name\":\"闰日\",\"date\":\"2024-02-29\",\"mode\":\"countdown\",\"repeat\":\"yearly\"}");
    assert(pc_anniversary_status(&a,(pc_date_t){2027,2,1},&s) && s.days==27 && s.next.day==28 && s.years==3);
    assert(pc_anniversary_status(&a,(pc_date_t){2028,2,28},&s) && s.days==1 && s.next.day==29);
    /* One-off countdown: upcoming, today, then passed. */
    a=anniversary("{\"id\":\"exam\",\"name\":\"考试\",\"date\":\"2026-12-20\",\"mode\":\"countdown\",\"repeat\":\"once\"}");
    assert(pc_anniversary_status(&a,(pc_date_t){2026,12,10},&s) && s.state==PC_ANNIV_UPCOMING && s.days==10);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,12,25},&s) && s.state==PC_ANNIV_PASSED && s.days==5 && !s.celebrate);
    /* Elapsed: day 0, every 100 days and yearly anniversaries are celebrated. */
    a=anniversary("{\"id\":\"baby\",\"name\":\"宝宝\",\"date\":\"2026-01-01\",\"mode\":\"elapsed\",\"repeat\":\"yearly\"}");
    assert(pc_anniversary_status(&a,(pc_date_t){2025,12,30},&s) && s.state==PC_ANNIV_UPCOMING && s.days==2);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,1,1},&s) && s.state==PC_ANNIV_TODAY && s.celebrate);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,4,11},&s) && s.state==PC_ANNIV_SINCE && s.days==100 && s.celebrate);
    assert(pc_anniversary_status(&a,(pc_date_t){2026,4,12},&s) && s.days==101 && !s.celebrate);
    assert(pc_anniversary_status(&a,(pc_date_t){2027,1,1},&s) && s.days==365 && s.years==1 && s.celebrate);
    /* Lunar yearly: 正月初一 2026 is 2026-02-17; a lunar 30th falls back to the 29th (除夕 2026-02-16). */
    a=anniversary("{\"id\":\"lny\",\"name\":\"农历生日\",\"date\":\"2000-02-05\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"lunar\":{\"month\":1,\"day\":1}}");
    assert(pc_anniversary_status(&a,(pc_date_t){2026,2,10},&s) && s.days==7 && s.next.month==2 && s.next.day==17 && s.years==26);
    a=anniversary("{\"id\":\"eve\",\"name\":\"除夕\",\"date\":\"2000-02-04\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"lunar\":{\"month\":12,\"day\":30}}");
    assert(pc_anniversary_status(&a,(pc_date_t){2026,1,20},&s) && s.next.year==2026 && s.next.month==2 && s.next.day==16);
    /* Rejected input. */
    assert(!pc_anniversary_parse_item("{\"id\":\"Bad\",\"name\":\"x\",\"date\":\"2026-01-01\",\"mode\":\"countdown\",\"repeat\":\"once\"}",&a,error,sizeof(error)));
    assert(!pc_anniversary_parse_item("{\"id\":\"a\",\"name\":\"x\",\"date\":\"2026-02-30\",\"mode\":\"countdown\",\"repeat\":\"once\"}",&a,error,sizeof(error)));
    assert(!pc_anniversary_parse_item("{\"id\":\"a\",\"name\":\"x\",\"date\":\"2026-01-01\",\"mode\":\"countdown\",\"repeat\":\"once\",\"lunar\":{\"month\":1,\"day\":1}}",&a,error,sizeof(error)));
    assert(!pc_anniversary_parse_item("{\"id\":\"a\",\"name\":\"x\",\"date\":\"2026-01-01\",\"mode\":\"countdown\",\"repeat\":\"once\",\"band\":\"pink\"}",&a,error,sizeof(error)));
    assert(!pc_anniversary_parse_item("{\"id\":\"a\",\"name\":\"x\",\"date\":\"2026-01-01\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"melody\":\"beep\"}",&a,error,sizeof(error)));
    a=anniversary("{\"id\":\"song\",\"name\":\"生日\",\"date\":\"2020-01-01\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"melody\":\"canon\"}");
    assert(!strcmp(a.melody,"canon"));
    assert(!pc_anniversary_parse_item("{\"id\":\"a\",\"name\":\"\\u00ff\\u00fe\",\"date\":\"2026-01-01\",\"mode\":\"sideways\",\"repeat\":\"once\"}",&a,error,sizeof(error)));
    /* List round trip, upsert, remove and capacity. */
    pc_anniversaries_t *list=calloc(1,sizeof(*list)),*again=calloc(1,sizeof(*again)); assert(list && again);
    char id[17];
    for (int i=0;i<PC_ANNIV_MAX;i++) {
        pc_anniversary_t item=anniversary("{\"id\":\"x\",\"name\":\"名\",\"date\":\"2026-01-01\",\"mode\":\"elapsed\",\"repeat\":\"once\"}");
        snprintf(id,sizeof(id),"x%d",i); strcpy(item.id,id); assert(pc_anniversaries_upsert(list,&item));
    }
    assert(!pc_anniversaries_upsert(list,&a)); assert(pc_anniversaries_upsert(list,&list->items[3]));
    char *json=pc_anniversaries_json(list); assert(json);
    assert(pc_anniversaries_parse(json,again,error,sizeof(error)) && again->count==PC_ANNIV_MAX);
    assert(!memcmp(list,again,sizeof(*list))); free(json);
    assert(pc_anniversaries_remove(list,"x0") && list->count==PC_ANNIV_MAX-1 && !strcmp(list->items[0].id,"x1"));
    assert(!pc_anniversaries_remove(list,"x0"));
    assert(pc_anniversaries_today(list,(pc_date_t){2026,1,1})==0);
    assert(pc_anniversaries_today(list,(pc_date_t){2026,1,2})==-1);
    assert(!pc_anniversaries_parse("[{\"id\":\"a\",\"name\":\"x\",\"date\":\"2026-01-01\",\"mode\":\"elapsed\",\"repeat\":\"once\"},{\"id\":\"a\",\"name\":\"y\",\"date\":\"2026-01-01\",\"mode\":\"elapsed\",\"repeat\":\"once\"}]",again,error,sizeof(error)));
    assert(!pc_anniversaries_parse("{}",again,error,sizeof(error)));
    free(list); free(again);
    uint8_t *packed=malloc(PC_PACKED_BYTES),*pixels=malloc(PC_PIXELS); assert(packed && pixels);
    memset(packed,0x25,PC_PACKED_BYTES); assert(pc_packed_valid(packed));
    pc_packed_unpack(packed,pixels); assert(pixels[0]==PC_RED && pixels[1]==PC_GREEN && pixels[PC_PIXELS-1]==PC_GREEN);
    packed[PC_PACKED_BYTES-1]=0x26; assert(!pc_packed_valid(packed));
    free(packed); free(pixels);
}
static void config_tests(void)
{
    pc_config_t c={0},back;
    strcpy(c.ssid,"Home"); strcpy(c.password,"password1"); strcpy(c.host,"abc.qweatherapi.com"); strcpy(c.api_key,"KEY_123-x");
    strcpy(c.city,"深圳"); assert(pc_config_resolvable(&c)); assert(!pc_config_valid(&c));
    strcpy(c.location,"101280601"); c.latitude=22.54; c.longitude=114.06; c.coordinates=true; assert(pc_config_valid(&c));
    char *json=pc_config_to_json(&c); assert(json);
    assert(pc_config_from_json(json,&back)); free(json);
    assert(!strcmp(back.ssid,c.ssid) && !strcmp(back.password,c.password) && !strcmp(back.api_key,c.api_key));
    assert(!strcmp(back.city,"深圳") && !strcmp(back.location,"101280601") && back.coordinates && back.latitude==22.54);
    assert(!back.audio.keys && !back.audio.boot && back.audio.volume==60 && !strcmp(back.audio.chime,"off") && !strcmp(back.audio.anniv,"off"));
    c.audio.volume=40; c.audio.keys=true; c.audio.boot=true; c.audio.chime_start=9; c.audio.chime_end=20;
    strcpy(c.audio.chime,"canon"); strcpy(c.audio.anniv,"birthday");
    json=pc_config_to_json(&c); assert(json);
    assert(pc_config_from_json(json,&back)); free(json);
    assert(back.audio.volume==40 && back.audio.keys && back.audio.boot && back.audio.chime_start==9 && back.audio.chime_end==20);
    assert(!strcmp(back.audio.chime,"canon") && !strcmp(back.audio.anniv,"birthday"));
    assert(pc_config_from_json("{\"audio\":{\"chime\":\"nope\",\"volume\":3,\"keys\":true}}",&back));
    assert(back.audio.keys && !strcmp(back.audio.chime,"off") && back.audio.volume==60 && !back.audio.boot);
    /* The card example: name only, coordinates resolved on first connection. */
    char *example=read_file("sdcard/papercolor/config.example.json");
    assert(pc_config_from_json(example,&back) && !back.coordinates && !strcmp(back.city,"深圳") && !pc_config_valid(&back));
    assert(!back.audio.keys && !back.audio.boot && !strcmp(back.audio.chime,"off") && !strcmp(back.audio.anniv,"off"));
    free(example);
    assert(!pc_config_from_json("[",&back) && !pc_config_from_json("[]",&back));
    strcpy(c.password,"short"); assert(!pc_config_resolvable(&c));
    strcpy(c.password,""); strcpy(c.api_key,"bad key"); assert(!pc_config_resolvable(&c));
    pc_config_t city={0};
    assert(pc_city_parse("{\"code\":\"200\",\"location\":[{\"id\":\"1\",\"name\":\"Tokyo\",\"lat\":\"35\",\"lon\":\"139\",\"tz\":\"Asia/Tokyo\"},"
        "{\"id\":\"101280601\",\"name\":\"深圳\",\"lat\":\"22.54\",\"lon\":\"114.06\",\"tz\":\"Asia/Shanghai\"}]}",&city));
    assert(!strcmp(city.location,"101280601") && city.coordinates && city.longitude==114.06);
    assert(!pc_city_parse("{\"code\":\"404\"}",&city));
}
static void font_tests(void)
{
    char missing[97];
    pc_fonts_open(NULL);
    /* 璨 is GB2312 (built-in 18 px) but not in the UI subset used for 24/36. */
    assert(pc_render_missing("璨",18,missing,sizeof(missing))==0);
    assert(pc_render_missing("璨a",36,missing,sizeof(missing))==1 && !strcmp(missing,"璨"));
    assert(pc_render_missing("𠀀",18,missing,sizeof(missing))==1);
    /* run_host_checks.py copies the 18 px pack as a fake 36 px card font. */
    pc_fonts_open("tests/.build/cardfonts");
    assert(pc_fonts_available(36) && !pc_fonts_available(24) && !pc_fonts_available(14));
    assert(pc_fonts_has(36,0x74a8) && !pc_fonts_has(36,0x20000));
    pc_glyph_t g; assert(pc_fonts_glyph(36,0x74a8,&g) && g.w && g.runs);
    assert(pc_render_missing("璨𠀀璨",36,missing,sizeof(missing))==1 && !strcmp(missing,"𠀀"));
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
static void audio_tests(void)
{
    pc_audio_config_t cfg;
    pc_audio_defaults(&cfg);
    assert(cfg.volume==60 && !cfg.keys && !cfg.boot && cfg.chime_start==8 && cfg.chime_end==21);
    assert(!strcmp(cfg.chime,"off") && !strcmp(cfg.anniv,"off") && !pc_chime_hour_allowed(&cfg,8));
    strcpy(cfg.chime,"bell");
    assert(pc_chime_hour_allowed(&cfg,8) && pc_chime_hour_allowed(&cfg,21) && !pc_chime_hour_allowed(&cfg,22));
    cfg.chime_start=22; cfg.chime_end=7;
    assert(pc_chime_hour_allowed(&cfg,23) && pc_chime_hour_allowed(&cfg,0) && pc_chime_hour_allowed(&cfg,7));
    assert(!pc_chime_hour_allowed(&cfg,12) && !pc_chime_hour_allowed(NULL,8) && !pc_chime_hour_allowed(&cfg,-1));
    assert(pc_melody_valid("off") && pc_melody_valid("castle_sky") && !pc_melody_valid("_boot") && !pc_melody_valid("nope"));
    int playable=0;
    for (size_t i=0;i<pc_melody_count();i++) {
        const pc_melody_info_t *m=pc_melody_at(i);
        assert(m && m->count && m->notes && pc_melody_lookup(m->id)==m && pc_synth_render(m->id,60,NULL,0)>0);
        if (m->id[0]!='_') { assert(pc_melody_valid(m->id)); playable++; }
    }
    assert(playable==14 && !pc_melody_lookup("off") && !pc_synth_render("off",60,NULL,0) && !pc_melody_at(pc_melody_count()));
    size_t n=pc_synth_render("bell",60,NULL,0);
    int16_t *pcm=calloc(n,sizeof(*pcm)),*again=calloc(n,sizeof(*again));
    assert(pcm && again && pc_synth_render("bell",60,pcm,n)==n);
    assert(pc_synth_render("bell",60,again,n)==n && !memcmp(pcm,again,n*sizeof(*pcm)));
    int peak=0;
    for (size_t i=0;i<n;i++) { int v=pcm[i]<0?-pcm[i]:pcm[i]; if (v>peak) peak=v; }
    assert(peak>4000 && peak<32767);
    assert(pc_synth_render("bell",0,pcm,n)==n);
    for (size_t i=0;i<n;i++) assert(!pcm[i]);
    free(pcm); free(again);
    n=pc_synth_render("birthday",80,NULL,0);
    pcm=calloc(n,sizeof(*pcm));
    assert(pcm && n<PC_AUDIO_SAMPLE_RATE*15 && pc_synth_render("birthday",80,pcm,n)==n);
    for (size_t i=0;i<8;i++) assert((pcm[i]<0?-pcm[i]:pcm[i])<4000);
    peak=0;
    for (size_t i=0;i<n;i++) { int v=pcm[i]<0?-pcm[i]:pcm[i]; if (v>peak) peak=v; }
    assert(peak>8000 && peak<=32767);
    free(pcm);
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
static void save_raw(const char *name,const uint8_t *pixels)
{
    char path[100]; snprintf(path,sizeof(path),"tests/.build/%s.raw",name);
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(pixels,1,PC_PIXELS,f)==PC_PIXELS); fclose(f);
}
static void draft_tests(void)
{
    uint8_t *pixels=malloc(PC_PIXELS),*back=malloc(PC_PIXELS),*body=malloc(PC_DRAFT_MAX_BODY+1);
    assert(pixels && back && body);
    for (int i=0;i<PC_PIXELS;i++) pixels[i]=(i*7+i/400)%6;
    const char *json="{\"id\":\"wed\",\"name\":\"结婚纪念日\",\"date\":\"2020-05-20\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"image\":true}";
    size_t n=strlen(json);
    body[0]=n&255; body[1]=n>>8; memcpy(body+2,json,n);
    pc_packed_pack(pixels,body+2+n);
    assert(pc_packed_valid(body+2+n));
    pc_packed_unpack(body+2+n,back); assert(!memcmp(pixels,back,PC_PIXELS));
    pc_anniversary_t item; const uint8_t *image; char error[96];
    assert(pc_draft_parse(body,2+n+PC_PACKED_BYTES,&item,&image,error,sizeof(error)) && image==body+2+n && item.image);
    assert(pc_draft_parse(body,2+n,&item,&image,error,sizeof(error)) && !image);
    /* Truncated image, trailing byte, short header, zero or oversized JSON length, colour 6, bad JSON. */
    assert(!pc_draft_parse(body,2+n+PC_PACKED_BYTES-1,&item,&image,error,sizeof(error)));
    assert(!pc_draft_parse(body,2+n+1,&item,&image,error,sizeof(error)));
    assert(!pc_draft_parse(body,1,&item,&image,error,sizeof(error)));
    assert(!pc_draft_parse(body,n,&item,&image,error,sizeof(error)));
    body[0]=0; body[1]=0; assert(!pc_draft_parse(body,2+n,&item,&image,error,sizeof(error)));
    body[0]=(PC_DRAFT_MAX_JSON+1)&255; body[1]=(PC_DRAFT_MAX_JSON+1)>>8;
    assert(!pc_draft_parse(body,PC_DRAFT_MAX_BODY,&item,&image,error,sizeof(error)));
    body[0]=n&255; body[1]=n>>8; body[2+n+77]=0x60;
    assert(!pc_draft_parse(body,2+n+PC_PACKED_BYTES,&item,&image,error,sizeof(error)));
    body[2]='['; assert(!pc_draft_parse(body,2+n,&item,&image,error,sizeof(error)));
    free(pixels); free(back); free(body);
}
static void anniversary_previews(void)
{
    uint8_t *pixels=malloc(PC_PIXELS),*packed=malloc(PC_PACKED_BYTES); assert(pixels && packed);
    /* Real card fonts when generated (tools/prepare_sdcard.py), otherwise the fake 36 px pack. */
    pc_fonts_open("sdcard/papercolor/fonts");
    if (!pc_fonts_available(36)) pc_fonts_open("tests/.build/cardfonts");
    /* Stand-in photo: the imaging pipeline's own output when present, else colour bars. */
    FILE *f=fopen("tests/.build/e6-mix.raw","rb");
    if (f) {
        uint8_t *idx=malloc(PC_PIXELS); assert(idx && fread(idx,1,PC_PIXELS,f)==PC_PIXELS); fclose(f);
        for (int i=0;i<PC_PACKED_BYTES;i++) packed[i]=idx[2*i]<<4|idx[2*i+1];
        free(idx);
    } else for (int i=0;i<PC_PACKED_BYTES;i++) { int c=(i*2%PC_WIDTH)/67; packed[i]=c<<4|c; }
    pc_anniversary_t wed=anniversary("{\"id\":\"wed\",\"name\":\"结婚纪念日\",\"date\":\"2020-05-20\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"image\":true,\"band\":\"white\"}");
    pc_anniversary_t baby=anniversary("{\"id\":\"baby\",\"name\":\"宝宝来到我们家\",\"date\":\"2026-01-01\",\"mode\":\"elapsed\",\"repeat\":\"yearly\",\"band\":\"yellow\"}");
    pc_anniversary_t lny=anniversary("{\"id\":\"mom\",\"name\":\"妈妈生日\",\"date\":\"1960-02-28\",\"mode\":\"countdown\",\"repeat\":\"yearly\",\"lunar\":{\"month\":2,\"day\":3},\"image\":true,\"overlay\":\"top\",\"band\":\"red\"}");
    pc_anniversary_t old=anniversary("{\"id\":\"since\",\"name\":\"一个非常非常非常长的纪念日名称会被截断\",\"date\":\"1990-01-01\",\"mode\":\"elapsed\",\"repeat\":\"once\",\"band\":\"blue\"}");
    struct { const char *name; const pc_anniversary_t *a; pc_date_t today; bool card, image; int battery; } cases[]={
        {"anniv-photo",&wed,{2026,10,15},true,true,80},
        {"anniv-today",&baby,{2026,4,11},true,false,80},
        {"anniv-lunar-top",&lny,{2026,10,15},true,true,12},
        {"anniv-long",&old,{2026,10,15},true,false,80},
        {"anniv-empty",NULL,{2026,10,15},true,false,80},
        {"anniv-no-card",NULL,{2026,10,15},false,false,80},
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        pc_anniv_screen_t s={.today=cases[i].today,.time_valid=true,.card=cases[i].card,.item=cases[i].a,.index=i,.count=4,.battery_percent=cases[i].battery};
        if (s.item) assert(pc_anniversary_status(s.item,s.today,&s.status));
        memset(pixels,0x7f,PC_PIXELS);
        pc_render_anniversary(pixels,&s,cases[i].image?packed:NULL);
        for (int p=0;p<PC_PIXELS;p++) assert(pixels[p]<6);
        save_raw(cases[i].name,pixels);
    }
    free(pixels); free(packed);
}
int main(void)
{
    setenv("TZ","CST-8",1); tzset();
    calendar_tests(); pc_holidays_t *table=holiday_tests(); button_tests(); action_tests(); weather_tests(); gzip_tests(); icon_tests();
    anniversary_tests(); draft_tests(); config_tests(); font_tests(); audio_tests(); previews(table); anniversary_previews(); pc_holidays_free(table);
    pc_fonts_open(NULL);
    puts("Calendar, holidays, buttons, weather, anniversary, config, font, audio and render checks passed");
}
