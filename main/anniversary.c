#include "anniversary.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *BANDS[]={"black","white","red","yellow","blue","green"};

bool pc_anniversary_id_valid(const char *id)
{
    size_t n=id?strlen(id):0;
    if (!n || n>16) return false;
    for (size_t i=0;i<n;i++) if (!((id[i]>='a'&&id[i]<='z')||(id[i]>='0'&&id[i]<='9'))) return false;
    return true;
}
static bool fail(char *error,size_t capacity,const char *reason)
{
    if (capacity) snprintf(error,capacity,"%s",reason);
    return false;
}
static const char *string(const cJSON *object,const char *key)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(v)?v->valuestring:NULL;
}
static bool utf8_valid(const char *s)
{
    const unsigned char *p=(const unsigned char *)s;
    while (*p) {
        int n=*p<0x80?1:(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:(*p&0xf8)==0xf0?4:0;
        if (!n) return false;
        for (int i=1;i<n;i++) if ((p[i]&0xc0)!=0x80) return false;
        p+=n;
    }
    return true;
}
static bool from_object(const cJSON *o,pc_anniversary_t *out,char *error,size_t capacity)
{
    pc_anniversary_t a={0};
    const char *id=string(o,"id"),*name=string(o,"name"),*date=string(o,"date");
    const char *mode=string(o,"mode"),*repeat=string(o,"repeat"),*overlay=string(o,"overlay"),*band=string(o,"band");
    if (!cJSON_IsObject(o) || !pc_anniversary_id_valid(id)) return fail(error,capacity,"纪念日 ID 无效");
    if (!name || !*name || strlen(name)>=sizeof(a.name) || !utf8_valid(name)) return fail(error,capacity,"名称为空或过长");
    if (!pc_parse_date(date,&a.date)) return fail(error,capacity,"日期格式应为 YYYY-MM-DD");
    if (!mode || (strcmp(mode,"countdown") && strcmp(mode,"elapsed"))) return fail(error,capacity,"类型应为倒数或经历");
    if (!repeat || (strcmp(repeat,"yearly") && strcmp(repeat,"once"))) return fail(error,capacity,"重复方式无效");
    strcpy(a.id,id); strcpy(a.name,name);
    a.mode=strcmp(mode,"elapsed")?PC_ANNIV_COUNTDOWN:PC_ANNIV_ELAPSED;
    a.yearly=!strcmp(repeat,"yearly");
    const cJSON *lunar=cJSON_GetObjectItemCaseSensitive(o,"lunar");
    if (cJSON_IsObject(lunar)) {
        const cJSON *m=cJSON_GetObjectItemCaseSensitive(lunar,"month"),*d=cJSON_GetObjectItemCaseSensitive(lunar,"day");
        if (!cJSON_IsNumber(m) || !cJSON_IsNumber(d) || m->valueint<1 || m->valueint>12 || d->valueint<1 || d->valueint>30)
            return fail(error,capacity,"农历月日无效");
        if (!a.yearly) return fail(error,capacity,"农历只用于每年重复");
        a.lunar=true; a.lunar_month=m->valueint; a.lunar_day=d->valueint;
    } else if (lunar && !cJSON_IsNull(lunar)) return fail(error,capacity,"农历字段无效");
    a.image=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"image"));
    a.overlay_top=overlay && !strcmp(overlay,"top");
    a.band=PC_WHITE;
    if (band) {
        int i=0; while (i<6 && strcmp(band,BANDS[i])) i++;
        if (i==6) return fail(error,capacity,"文字条颜色无效");
        a.band=(pc_color_t)i;
    }
    const char *melody=string(o,"melody");
    if (melody && *melody) {
        if (!pc_melody_valid(melody)) return fail(error,capacity,"纪念日音乐无效");
        strcpy(a.melody,melody);
    }
    *out=a; return true;
}
bool pc_anniversary_parse_item(const char *json,pc_anniversary_t *out,char *error,size_t capacity)
{
    cJSON *root=cJSON_Parse(json);
    bool ok=root ? from_object(root,out,error,capacity) : fail(error,capacity,"JSON 格式错误");
    cJSON_Delete(root); return ok;
}
bool pc_anniversaries_parse(const char *json,pc_anniversaries_t *out,char *error,size_t capacity)
{
    cJSON *root=cJSON_Parse(json);
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return fail(error,capacity,"应为 JSON 数组"); }
    if (cJSON_GetArraySize(root)>PC_ANNIV_MAX) { cJSON_Delete(root); return fail(error,capacity,"纪念日数量超过上限"); }
    pc_anniversaries_t *list=calloc(1,sizeof(*list));
    if (!list) { cJSON_Delete(root); return fail(error,capacity,"内存不足"); }
    const cJSON *item; bool ok=true;
    cJSON_ArrayForEach(item,root) {
        pc_anniversary_t a;
        if (!from_object(item,&a,error,capacity)) { ok=false; break; }
        if (pc_anniversaries_find(list,a.id)>=0) { ok=fail(error,capacity,"纪念日 ID 重复"); break; }
        list->items[list->count++]=a;
    }
    if (ok) *out=*list;
    free(list); cJSON_Delete(root); return ok;
}
char *pc_anniversaries_json(const pc_anniversaries_t *list)
{
    cJSON *root=cJSON_CreateArray();
    if (!root) return NULL;
    for (int i=0;i<list->count;i++) {
        const pc_anniversary_t *a=&list->items[i];
        cJSON *o=cJSON_CreateObject();
        if (!o) { cJSON_Delete(root); return NULL; }
        cJSON_AddItemToArray(root,o);
        char date[16]; snprintf(date,sizeof(date),"%04d-%02d-%02d",a->date.year,a->date.month,a->date.day);
        cJSON_AddStringToObject(o,"id",a->id); cJSON_AddStringToObject(o,"name",a->name);
        cJSON_AddStringToObject(o,"date",date);
        cJSON_AddStringToObject(o,"mode",a->mode==PC_ANNIV_ELAPSED?"elapsed":"countdown");
        cJSON_AddStringToObject(o,"repeat",a->yearly?"yearly":"once");
        if (a->lunar) {
            cJSON *l=cJSON_AddObjectToObject(o,"lunar");
            cJSON_AddNumberToObject(l,"month",a->lunar_month); cJSON_AddNumberToObject(l,"day",a->lunar_day);
        } else cJSON_AddNullToObject(o,"lunar");
        cJSON_AddBoolToObject(o,"image",a->image);
        cJSON_AddStringToObject(o,"overlay",a->overlay_top?"top":"bottom");
        cJSON_AddStringToObject(o,"band",BANDS[a->band]);
        if (a->melody[0]) cJSON_AddStringToObject(o,"melody",a->melody);
    }
    char *json=cJSON_PrintUnformatted(root); cJSON_Delete(root); return json;
}
int pc_anniversaries_find(const pc_anniversaries_t *list,const char *id)
{
    for (int i=0;i<list->count;i++) if (!strcmp(list->items[i].id,id)) return i;
    return -1;
}
bool pc_anniversaries_upsert(pc_anniversaries_t *list,const pc_anniversary_t *item)
{
    int i=pc_anniversaries_find(list,item->id);
    if (i<0) { if (list->count>=PC_ANNIV_MAX) return false; i=list->count++; }
    list->items[i]=*item; return true;
}
bool pc_anniversaries_remove(pc_anniversaries_t *list,const char *id)
{
    int i=pc_anniversaries_find(list,id);
    if (i<0) return false;
    memmove(&list->items[i],&list->items[i+1],(list->count-i-1)*sizeof(list->items[0]));
    list->count--; return true;
}
/* Feb 29 falls back to Feb 28 in common years. */
static pc_date_t solar_in_year(pc_date_t d,int year)
{
    pc_date_t r={year,d.month,d.day};
    if (r.day>pc_month_days(year,r.month)) r.day=pc_month_days(year,r.month);
    return r;
}
/* A lunar 30th falls back to the 29th in short months. */
static bool lunar_match(int day,int month,int target_day)
{
    pc_lunar_t l,next;
    if (!pc_lunar_lookup(pc_date_from_day(day),&l) || l.leap || l.month!=month) return false;
    if (l.day==target_day) return true;
    return target_day==30 && l.day==29 && pc_lunar_lookup(pc_date_from_day(day+1),&next) && next.day==1;
}
static bool next_occurrence(const pc_anniversary_t *a,pc_date_t today,pc_date_t *next,int *years)
{
    int from=pc_day_number(today);
    if (!a->lunar) {
        pc_date_t d=solar_in_year(a->date,today.year);
        if (pc_day_number(d)<from) d=solar_in_year(a->date,today.year+1);
        *next=d; *years=d.year-a->date.year; return true;
    }
    for (int day=from;day<from+400;day++) {
        if (!lunar_match(day,a->lunar_month,a->lunar_day)) continue;
        *next=pc_date_from_day(day);
        pc_lunar_t now,base;
        if (!pc_lunar_lookup(*next,&now)) return false;
        int base_year=pc_lunar_lookup(a->date,&base)?base.year:
            a->date.year-(a->date.month<=2 && a->lunar_month>=10);
        *years=now.year-base_year; return true;
    }
    return false;
}
bool pc_anniversary_status(const pc_anniversary_t *a,pc_date_t today,pc_anniv_status_t *out)
{
    if (!pc_date_valid(today) || !pc_date_valid(a->date)) return false;
    pc_anniv_status_t s={0};
    int now=pc_day_number(today),base=pc_day_number(a->date);
    if (a->yearly && !next_occurrence(a,today,&s.next,&s.years)) return false;
    if (!a->yearly) s.next=a->date;
    if (a->mode==PC_ANNIV_COUNTDOWN) {
        int target=a->yearly?pc_day_number(s.next):base;
        s.days=target-now;
        s.state=s.days>0?PC_ANNIV_UPCOMING:s.days==0?PC_ANNIV_TODAY:PC_ANNIV_PASSED;
        if (s.days<0) s.days=-s.days;
        s.celebrate=s.state==PC_ANNIV_TODAY;
    } else if (now<base) {
        s.state=PC_ANNIV_UPCOMING; s.days=base-now; s.next=a->date;
    } else {
        s.days=now-base;
        s.state=s.days==0?PC_ANNIV_TODAY:PC_ANNIV_SINCE;
        bool anniversary=a->yearly && pc_day_number(s.next)==now && s.years>0;
        s.celebrate=s.days==0 || anniversary || s.days%100==0;
    }
    if (s.years<0) s.years=0;
    *out=s; return true;
}
int pc_anniversaries_today(const pc_anniversaries_t *list,pc_date_t today)
{
    for (int i=0;i<list->count;i++) {
        pc_anniv_status_t s;
        if (pc_anniversary_status(&list->items[i],today,&s) && s.celebrate) return i;
    }
    return -1;
}
bool pc_packed_valid(const uint8_t *packed)
{
    for (size_t i=0;i<PC_PACKED_BYTES;i++) if ((packed[i]>>4)>=6 || (packed[i]&15)>=6) return false;
    return true;
}
void pc_packed_unpack(const uint8_t *packed,uint8_t *pixels)
{
    for (size_t i=0;i<PC_PACKED_BYTES;i++) { pixels[2*i]=packed[i]>>4; pixels[2*i+1]=packed[i]&15; }
}
void pc_packed_pack(const uint8_t *pixels,uint8_t *packed)
{
    for (size_t i=0;i<PC_PACKED_BYTES;i++) packed[i]=(uint8_t)((pixels[2*i]&15)<<4|(pixels[2*i+1]&15));
}
bool pc_draft_parse(const uint8_t *body,size_t length,pc_anniversary_t *item,const uint8_t **image,
                    char *error,size_t capacity)
{
    if (!body || length<2) return fail(error,capacity,"请求为空");
    size_t n=body[0]|(size_t)body[1]<<8,rest=length>=2+n?length-2-n:0;
    if (!n || n>PC_DRAFT_MAX_JSON || length<2+n) return fail(error,capacity,"纪念日 JSON 长度无效");
    if (rest && rest!=PC_PACKED_BYTES) return fail(error,capacity,"草稿图片大小不正确");
    if (rest && !pc_packed_valid(body+2+n)) return fail(error,capacity,"草稿图片含无效颜色");
    char *json=malloc(n+1);
    if (!json) return fail(error,capacity,"内存不足");
    memcpy(json,body+2,n); json[n]=0;
    bool ok=pc_anniversary_parse_item(json,item,error,capacity);
    free(json);
    if (ok) *image=rest?body+2+n:NULL;
    return ok;
}
