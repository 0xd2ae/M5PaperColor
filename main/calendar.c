#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

extern const uint8_t pc_lunar_start[] __asm__("_binary_lunar_bin_start");
extern const uint8_t pc_lunar_end[] __asm__("_binary_lunar_bin_end");
extern const uint8_t pc_terms_start[] __asm__("_binary_terms_bin_start");
extern const uint8_t pc_terms_end[] __asm__("_binary_terms_bin_end");

int pc_month_days(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
bool pc_date_valid(pc_date_t d)
{
    return d.year >= 1900 && d.year <= 2199 && d.month >= 1 && d.month <= 12 &&
           d.day >= 1 && d.day <= pc_month_days(d.year, d.month);
}
/* Gregorian civil-day conversion, epoch 1970-01-01. */
int pc_day_number(pc_date_t d)
{
    int y = d.year - (d.month <= 2), era = y / 400, yo = y - era * 400;
    int mp = d.month + (d.month > 2 ? -3 : 9);
    int doy = (153 * mp + 2) / 5 + d.day - 1;
    return era * 146097 + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
}
pc_date_t pc_date_from_day(int day)
{
    int z = day + 719468, era = z / 146097, doe = z - era * 146097;
    int yo = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yo + era * 400, doy = doe - (365 * yo + yo / 4 - yo / 100);
    int mp = (5 * doy + 2) / 153;
    return (pc_date_t){y + (mp >= 10), mp + (mp < 10 ? 3 : -9), doy - (153 * mp + 2) / 5 + 1};
}
int pc_weekday(pc_date_t d)
{
    int n = (pc_day_number(d) + 3) % 7;
    return n < 0 ? n + 7 : n;
}
bool pc_parse_date(const char *s, pc_date_t *out)
{
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
    pc_date_t d = {0};
    if (sscanf(s, "%4d-%2d-%2d", &d.year, &d.month, &d.day) != 3 || !pc_date_valid(d)) return false;
    *out = d;
    return true;
}
void pc_shift_month(int *year, int *month, int delta)
{
    int n = *year * 12 + *month - 1 + delta;
    if (n < 2000 * 12) n = 2000 * 12;
    if (n > 2099 * 12 + 11) n = 2099 * 12 + 11;
    *year = n / 12; *month = n % 12 + 1;
}
bool pc_lunar_lookup(pc_date_t date, pc_lunar_t *out)
{
    if (!pc_date_valid(date) || date.year < 2000 || date.year > 2099) return false;
    int index = pc_day_number(date) - pc_day_number((pc_date_t){2000,1,1});
    if ((size_t)(index + 1) * 4 > (size_t)(pc_lunar_end - pc_lunar_start)) return false;
    const uint8_t *p = pc_lunar_start + index * 4;
    int month = (int8_t)p[2];
    *out = (pc_lunar_t){p[0] | (p[1] << 8), abs(month), p[3], month < 0};
    return true;
}
void pc_lunar_label(pc_date_t date, bool full, char *out, size_t capacity)
{
    static const char *months[] = {"正","二","三","四","五","六","七","八","九","十","冬","腊"};
    static const char *digits[] = {"一","二","三","四","五","六","七","八","九","十"};
    pc_lunar_t d;
    if (!pc_lunar_lookup(date, &d)) { snprintf(out, capacity, "农历不支持"); return; }
    char day[20];
    if (d.day == 10) snprintf(day,sizeof(day),"初十");
    else if (d.day == 20) snprintf(day,sizeof(day),"二十");
    else if (d.day == 30) snprintf(day,sizeof(day),"三十");
    else snprintf(day,sizeof(day),"%s%s",d.day < 10 ? "初" : d.day < 20 ? "十" : "廿",digits[(d.day-1)%10]);
    if (full) snprintf(out,capacity,"%d年 %s%s月%s",d.year,d.leap?"闰":"",months[d.month-1],day);
    else if (d.day == 1) snprintf(out,capacity,"%s%s月",d.leap?"闰":"",months[d.month-1]);
    else snprintf(out,capacity,"%s",day);
}
const char *pc_festival(pc_date_t date)
{
    pc_lunar_t d, next;
    if (!pc_lunar_lookup(date,&d)) return "";
    if (pc_lunar_lookup(pc_date_from_day(pc_day_number(date)+1),&next) && !next.leap && next.month == 1 && next.day == 1) return "除夕";
    if (d.leap) return "";
    if (d.month == 1 && d.day == 1) return "春节";
    if (d.month == 1 && d.day == 15) return "元宵";
    if (d.month == 5 && d.day == 5) return "端午";
    if (d.month == 7 && d.day == 7) return "七夕";
    if (d.month == 8 && d.day == 15) return "中秋";
    if (d.month == 9 && d.day == 9) return "重阳";
    return "";
}
const char *pc_solar_term(pc_date_t date)
{
    static const char *names[] = {"小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨","立夏","小满","芒种","夏至",
                                  "小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降","立冬","小雪","大雪","冬至"};
    if (!pc_date_valid(date) || date.year < 2000 || date.year > 2099) return "";
    int index = (date.year - 2000) * 24 + (date.month - 1) * 2;
    if ((size_t)(index + 2) > (size_t)(pc_terms_end - pc_terms_start)) return "";
    if (pc_terms_start[index] == date.day) return names[(date.month - 1) * 2];
    if (pc_terms_start[index + 1] == date.day) return names[(date.month - 1) * 2 + 1];
    return "";
}

typedef struct { int start, end; char name[64]; } holiday_span_t;
struct pc_holidays {
    bool years[200]; /* 1900..2099 */
    size_t span_count, work_count;
    holiday_span_t spans[256];
    int work[2048];
};
static bool json_string(const cJSON *j, const char *key, const char **value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(j,key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    *value = item->valuestring;
    return true;
}
pc_holidays_t *pc_holidays_parse(const char *json, size_t length, char *error, size_t capacity)
{
    pc_holidays_t *table = NULL;
    cJSON *root = NULL;
    const char *reason = "年度表格式错误";
    if (!json || length == 0 || length > PC_HOLIDAY_MAX_JSON) goto fail;
    const char *end = NULL;
    root = cJSON_ParseWithLengthOpts(json,length,&end,false);
    if (!root) goto fail;
    while (end < json + length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) end++;
    if (end != json + length) goto fail;
    const char *zone;
    if (!json_string(root,"Timezone",&zone) || strcmp(zone,"Asia/Shanghai") != 0) goto fail;
    const cJSON *years = cJSON_GetObjectItemCaseSensitive(root,"Years");
    if (!cJSON_IsObject(years)) goto fail;
    table = calloc(1,sizeof(*table));
    if (!table) { reason = "内存不足"; goto fail; }
    const cJSON *year;
    cJSON_ArrayForEach(year,years) {
        if (!year->string || strlen(year->string) != 4) goto fail;
        for (int i=0;i<4;i++) if (year->string[i]<'0'||year->string[i]>'9') goto fail;
        int y = atoi(year->string);
        if (y < 1900 || y > 2099 || !cJSON_IsArray(year) || !cJSON_GetArraySize(year) || table->years[y-1900]) goto fail;
        table->years[y-1900] = true;
        const cJSON *item;
        cJSON_ArrayForEach(item,year) {
            const char *name,*start,*finish;
            pc_date_t a,b;
            if (!json_string(item,"Name",&name) || !name[0] || strlen(name)>=64 ||
                !json_string(item,"StartDate",&start) || !json_string(item,"EndDate",&finish) ||
                !pc_parse_date(start,&a) || !pc_parse_date(finish,&b)) goto fail;
            int begin = pc_day_number(a), last = pc_day_number(b);
            const cJSON *duration = cJSON_GetObjectItemCaseSensitive(item,"Duration");
            if (begin > last || last-begin>=31 || b.year!=y || a.year<y-1 ||
                !cJSON_IsNumber(duration) || duration->valuedouble!=last-begin+1 || table->span_count>=256) goto fail;
            for (size_t i=0;i<table->span_count;i++) if (begin<=table->spans[i].end && last>=table->spans[i].start) { reason="放假区间重复"; goto fail; }
            holiday_span_t *span = &table->spans[table->span_count++];
            span->start=begin; span->end=last; snprintf(span->name,sizeof(span->name),"%s",name);
            const cJSON *work = cJSON_GetObjectItemCaseSensitive(item,"CompDays");
            if (!cJSON_IsArray(work)) goto fail;
            const cJSON *wd;
            cJSON_ArrayForEach(wd,work) {
                pc_date_t d;
                if (!cJSON_IsString(wd) || !pc_parse_date(wd->valuestring,&d) || abs(d.year-y)>1 || table->work_count>=2048) goto fail;
                int n = pc_day_number(d);
                for (size_t i=0;i<table->work_count;i++) if (table->work[i]==n) { reason="补班日期重复"; goto fail; }
                table->work[table->work_count++]=n;
            }
        }
    }
    if (!table->span_count) goto fail;
    for (size_t w=0;w<table->work_count;w++) for (size_t s=0;s<table->span_count;s++)
        if (table->work[w]>=table->spans[s].start && table->work[w]<=table->spans[s].end) { reason="休与班日期冲突"; goto fail; }
    cJSON_Delete(root);
    return table;
fail:
    if (capacity) snprintf(error,capacity,"%s",reason);
    cJSON_Delete(root); free(table); return NULL;
}
void pc_holidays_free(pc_holidays_t *table) { free(table); }
bool pc_holidays_has_year(const pc_holidays_t *table,int year)
{
    return table && year>=1900 && year<=2099 && table->years[year-1900];
}
pc_day_kind_t pc_holidays_kind(const pc_holidays_t *table,pc_date_t date,const char **name)
{
    if (name) *name="";
    if (!table) return PC_NORMAL;
    int n=pc_day_number(date);
    for (size_t i=0;i<table->work_count;i++) if (table->work[i]==n) return PC_WORK;
    for (size_t i=0;i<table->span_count;i++) if (n>=table->spans[i].start && n<=table->spans[i].end) {
        if (name) *name=table->spans[i].name;
        return PC_REST;
    }
    return PC_NORMAL;
}
