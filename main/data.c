#include "model.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool pc_host_valid(const char *host)
{
    size_t n = host ? strlen(host) : 0;
    if (n < 4 || n >= 128 || host[0] == '.' || host[n-1] == '.' || !strchr(host,'.')) return false;
    for (size_t i=0;i<n;i++) if (!(isalnum((unsigned char)host[i]) || host[i]=='-' || host[i]=='.')) return false;
    return true;
}
bool pc_location_valid(const char *value)
{
    size_t n = value ? strlen(value) : 0;
    if (!n || n>=33) return false;
    for (size_t i=0;i<n;i++) if (!isalnum((unsigned char)value[i])) return false;
    return true;
}
bool pc_config_valid(const pc_config_t *c)
{
    if (!c->ssid[0] || !c->city[0] || !pc_host_valid(c->host) || !pc_location_valid(c->location) || !c->api_key[0] || !c->coordinates || !isfinite(c->latitude) || !isfinite(c->longitude) ||
        c->latitude < -90 || c->latitude > 90 || c->longitude < -180 || c->longitude > 180) return false;
    size_t pw = strlen(c->password);
    if (pw && (pw < 8 || pw > 63)) return false;
    for (const char *p=c->api_key;*p;p++) if (!isalnum((unsigned char)*p) && *p!='-' && *p!='_') return false;
    return true;
}
static bool text(const cJSON *object,const char *key,char *out,size_t cap)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(object,key);
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring)>=cap) return false;
    snprintf(out,cap,"%s",v->valuestring); return true;
}
static bool number(const cJSON *object,const char *key,int lo,int hi,int *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(object,key);
    if (!cJSON_IsString(v) || !v->valuestring || !*v->valuestring) return false;
    char *end; long n=strtol(v->valuestring,&end,10);
    if (*end || n<lo || n>hi) return false;
    *out=(int)n; return true;
}
static bool success(const cJSON *root)
{
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
    return cJSON_IsString(code) && !strcmp(code->valuestring,"200");
}
static bool quantity(const cJSON *parent, const char *key, int *out)
{
    const cJSON *q=cJSON_GetObjectItemCaseSensitive(parent,key);
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(q,"value");
    const cJSON *unit=cJSON_GetObjectItemCaseSensitive(q,"unit");
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < -100 || value->valuedouble > 100 ||
        !cJSON_IsString(unit) || strcmp(unit->valuestring,"°C")) return false;
    *out=(int)round(value->valuedouble); return true;
}
static bool weather_v1(const cJSON *a,const cJSON *b,const char *location,int64_t fetched,pc_weather_t *out)
{
    pc_weather_t result={0};
    const cJSON *condition=cJSON_GetObjectItemCaseSensitive(a,"condition");
    if (!quantity(a,"temperature",&result.temperature) || !text(condition,"text",result.text,sizeof(result.text)) ||
        !number(condition,"code",100,999,&result.icon)) return false;
    const cJSON *days=cJSON_GetObjectItemCaseSensitive(b,"days");
    if (!cJSON_IsArray(days) || cJSON_GetArraySize(days)<3) return false;
    for (int i=0;i<3;i++) {
        const cJSON *d=cJSON_GetArrayItem(days,i), *daytime=cJSON_GetObjectItemCaseSensitive(d,"daytime");
        const cJSON *cond=cJSON_GetObjectItemCaseSensitive(daytime,"condition");
        char start[48]; pc_date_t date;
        pc_forecast_t *p=&result.days[i];
        if (!text(d,"forecastStartTime",start,sizeof(start)) || strlen(start)<16) return false;
        memcpy(p->date,start,10); p->date[10]=0;
        if (!pc_parse_date(p->date,&date) || !quantity(d,"temperatureMin",&p->low) || !quantity(d,"temperatureMax",&p->high) ||
            p->low>p->high || !text(cond,"text",p->text,sizeof(p->text)) || !number(cond,"code",100,999,&p->icon)) return false;
        if (i) { pc_date_t prev; pc_parse_date(result.days[i-1].date,&prev); if (pc_day_number(date)!=pc_day_number(prev)+1) return false; }
    }
    result.valid=true; result.fetched=fetched;
    snprintf(result.location,sizeof(result.location),"%s",location);
    time_t epoch=(time_t)fetched; struct tm t; localtime_r(&epoch,&t);
    strftime(result.observation,sizeof(result.observation),"%Y-%m-%dT%H:%M+08:00",&t);
    *out=result; return true;
}
bool pc_weather_parse(const char *now,const char *daily,const char *location,int64_t fetched,pc_weather_t *out)
{
    cJSON *a=cJSON_Parse(now), *b=cJSON_Parse(daily);
    pc_weather_t result={0};
    bool ok=false;
    if (!pc_location_valid(location)) goto done;
    if (cJSON_HasObjectItem(a,"condition")) { ok=weather_v1(a,b,location,fetched,out); goto done; }
    if (!success(a) || !success(b)) goto done;
    const cJSON *n=cJSON_GetObjectItemCaseSensitive(a,"now"), *days=cJSON_GetObjectItemCaseSensitive(b,"daily");
    if (!number(n,"temp",-100,100,&result.temperature) || !number(n,"icon",100,999,&result.icon) ||
        !text(n,"text",result.text,sizeof(result.text)) || !text(n,"obsTime",result.observation,sizeof(result.observation)) ||
        !cJSON_IsArray(days) || cJSON_GetArraySize(days)<3) goto done;
    for (int i=0;i<3;i++) {
        const cJSON *d=cJSON_GetArrayItem(days,i);
        pc_forecast_t *p=&result.days[i]; pc_date_t date;
        if (!text(d,"fxDate",p->date,sizeof(p->date)) || !pc_parse_date(p->date,&date) ||
            !text(d,"textDay",p->text,sizeof(p->text)) || !number(d,"iconDay",100,999,&p->icon) ||
            !number(d,"tempMin",-100,100,&p->low) || !number(d,"tempMax",-100,100,&p->high) || p->low>p->high) goto done;
        if (i) { pc_date_t prev; pc_parse_date(result.days[i-1].date,&prev); if (pc_day_number(date)!=pc_day_number(prev)+1) goto done; }
    }
    result.valid=true; result.fetched=fetched;
    snprintf(result.location,sizeof(result.location),"%s",location);
    *out=result; ok=true;
done:
    cJSON_Delete(a); cJSON_Delete(b); return ok;
}
uint32_t pc_frame_hash(const uint8_t *pixels,size_t length)
{
    uint32_t hash=2166136261u;
    for (size_t i=0;i<length;i++) hash=(hash ^ pixels[i])*16777619u;
    return hash;
}
