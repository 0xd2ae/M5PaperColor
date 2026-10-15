/* Host renderer for the configuration-page fixture and tests.
 *
 * usage: render_cli HOLIDAYS.json [CARD_FONT_DIR] < request > frame
 * request: u16 little-endian JSON length, the scene JSON, then an optional 120000-byte packed image.
 * Scene keys: kind ("calendar"|"anniversary"), today ("YYYY-MM-DD"), time_valid, battery, card, offline,
 * weather (fixed sample forecast), stale, indoor, city, updated, year, month, index, count, item.
 * Writes the 120000-byte packed frame (high nibble first) to stdout. */
#include "model.h"
#include "render.h"
#include "anniversary.h"
#include "fonts.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_all(FILE *file,size_t *length)
{
    size_t capacity=1<<16,n=0; char *data=malloc(capacity);
    while (data) {
        n+=fread(data+n,1,capacity-n,file);
        if (n<capacity) break;
        char *grown=realloc(data,capacity*=2); if (!grown) { free(data); return NULL; } data=grown;
    }
    *length=n; return data;
}
static int fail(const char *message)
{
    fprintf(stderr,"render_cli: %s\n",message); return 1;
}
static bool flag(const cJSON *root,const char *key,bool fallback)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,key);
    return cJSON_IsBool(v)?cJSON_IsTrue(v):fallback;
}
static int number(const cJSON *root,const char *key,int fallback)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,key);
    return cJSON_IsNumber(v)?v->valueint:fallback;
}
static const char *text(const cJSON *root,const char *key,const char *fallback)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,key);
    return cJSON_IsString(v)&&v->valuestring?v->valuestring:fallback;
}
static void sample_weather(pc_weather_t *w,bool stale)
{
    static const char *labels[]={"晴","多云","小雨"};
    *w=(pc_weather_t){.valid=true,.temperature=24,.icon=100,.fetched=time(NULL)-(stale?4*3600:0)};
    strcpy(w->text,"晴");
    for (int i=0;i<3;i++) {
        strcpy(w->days[i].text,labels[i]); w->days[i].icon=i==0?100:i==1?101:305;
        w->days[i].low=18-i; w->days[i].high=27-i;
        snprintf(w->days[i].date,sizeof(w->days[i].date),"2026-10-%02d",13+i);
    }
}
int main(int argc,char **argv)
{
    if (argc<2) return fail("usage: render_cli HOLIDAYS.json [CARD_FONT_DIR] < request > frame");
    setenv("TZ","CST-8",1); tzset();
    FILE *f=fopen(argv[1],"rb"); if (!f) return fail("cannot open holidays");
    size_t n; char *json=read_all(f,&n); fclose(f); if (!json) return fail("cannot read holidays");
    char error[100];
    pc_holidays_t *holidays=pc_holidays_parse(json,n,error,sizeof(error)); free(json);
    if (!holidays) return fail(error);
    pc_fonts_open(argc>2?argv[2]:NULL);

    size_t length; uint8_t *body=(uint8_t *)read_all(stdin,&length);
    if (!body || length<2) return fail("empty request");
    size_t json_length=body[0]|body[1]<<8, rest=length-2-json_length;
    if (2+json_length>length || (rest && rest!=PC_PACKED_BYTES)) return fail("bad framing");
    const uint8_t *image=rest?body+2+json_length:NULL;
    if (image && !pc_packed_valid(image)) return fail("bad image");
    cJSON *scene=cJSON_ParseWithLength((const char *)body+2,json_length);
    if (!cJSON_IsObject(scene)) return fail("bad scene JSON");

    pc_date_t today;
    if (!pc_parse_date(text(scene,"today","2026-10-13"),&today)) return fail("bad today");
    bool time_valid=flag(scene,"time_valid",true), card=flag(scene,"card",true);
    int battery=number(scene,"battery",76);
    uint8_t *pixels=malloc(PC_PIXELS),*frame=malloc(PC_PACKED_BYTES);
    if (!pixels || !frame) return fail("out of memory");
    if (!strcmp(text(scene,"kind","calendar"),"calendar")) {
        pc_screen_t s={.today=today,.time_valid=time_valid,.battery_percent=battery,.card_missing=!card,
                       .view_year=number(scene,"year",today.year),.view_month=number(scene,"month",today.month),
                       .offline=flag(scene,"offline",false)};
        if (flag(scene,"weather",true)) sample_weather(&s.weather,flag(scene,"stale",false));
        s.indoor_valid=flag(scene,"indoor",true); s.indoor_temperature=23; s.indoor_humidity=58;
        snprintf(s.city,sizeof(s.city),"%s",text(scene,"city","深圳"));
        if (s.weather.valid) snprintf(s.updated,sizeof(s.updated),"%s",text(scene,"updated","更新 10-13 08:00"));
        pc_render(pixels,&s,holidays);
    } else {
        pc_anniversary_t item; bool has_item=false;
        const cJSON *o=cJSON_GetObjectItemCaseSensitive(scene,"item");
        if (cJSON_IsObject(o)) {
            char *item_json=cJSON_PrintUnformatted(o);
            has_item=item_json && pc_anniversary_parse_item(item_json,&item,error,sizeof(error));
            free(item_json);
            if (!has_item) return fail(error);
        }
        pc_anniv_screen_t s={.today=today,.time_valid=time_valid,.card=card,.battery_percent=battery,
                             .count=number(scene,"count",has_item),.index=number(scene,"index",0)};
        if (has_item) {
            s.item=&item;
            if (s.time_valid && !pc_anniversary_status(&item,today,&s.status)) s.time_valid=false;
        }
        pc_render_anniversary(pixels,&s,has_item && item.image?image:NULL);
    }
    pc_packed_pack(pixels,frame);
    bool ok=fwrite(frame,1,PC_PACKED_BYTES,stdout)==PC_PACKED_BYTES;
    cJSON_Delete(scene); free(body); free(pixels); free(frame); pc_holidays_free(holidays); pc_fonts_open(NULL);
    return ok?0:fail("write failed");
}
