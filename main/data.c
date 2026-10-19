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
bool pc_config_resolvable(const pc_config_t *c)
{
    if (!c->ssid[0] || !c->city[0] || !pc_host_valid(c->host) || !c->api_key[0]) return false;
    size_t pw=strlen(c->password);
    if (pw && (pw<8 || pw>63)) return false;
    for (const char *p=c->api_key;*p;p++) if (!isalnum((unsigned char)*p) && *p!='-' && *p!='_') return false;
    return true;
}
void pc_audio_defaults(pc_audio_config_t *a)
{
    *a=(pc_audio_config_t){.volume=60,.keys=false,.boot=false,.chime="off",.chime_start=8,.chime_end=21,.anniv="off"};
}
bool pc_melody_valid(const char *id)
{
    static const char *const IDS[]={
        "off","bell","westminster","cuckoo","birthday","jasmine","newyear","jingle",
        "auld_lang_syne","canon","wedding","ode_to_joy","fur_elise","clair_de_lune","castle_sky"
    };
    if (!id) return false;
    for (size_t i=0;i<sizeof(IDS)/sizeof(IDS[0]);i++) if (!strcmp(id,IDS[i])) return true;
    return false;
}
static void optional_text(const cJSON *object,const char *key,char *out,size_t cap)
{
    if (!text(object,key,out,cap)) out[0]=0;
}
int pc_config_wifi_find(const pc_config_t *c,const char *ssid)
{
    if (!c || !ssid || !ssid[0]) return -1;
    for (int i=0;i<c->wifi_count && i<PC_WIFI_MAX;i++) {
        if (!strcmp(c->wifis[i].ssid,ssid)) return i;
    }
    return -1;
}
void pc_config_sync_active_wifi(pc_config_t *c)
{
    if (!c) return;
    if (c->wifi_count<0) c->wifi_count=0;
    if (c->wifi_count>PC_WIFI_MAX) c->wifi_count=PC_WIFI_MAX;
    if (!c->ssid[0]) {
        if (c->wifi_count>0) pc_config_wifi_activate(c,c->wifis[0].ssid);
        return;
    }
    int idx=pc_config_wifi_find(c,c->ssid);
    if (idx>=0) {
        pc_wifi_profile_t *p=&c->wifis[idx];
        if (c->password[0]) strcpy(p->password,c->password);
        else if (p->password[0]) strcpy(c->password,p->password);
        if (c->city[0]) {
            strcpy(p->city,c->city); strcpy(p->location,c->location);
            p->latitude=c->latitude; p->longitude=c->longitude; p->coordinates=c->coordinates;
        } else if (p->city[0]) {
            strcpy(c->city,p->city); strcpy(c->location,p->location);
            c->latitude=p->latitude; c->longitude=p->longitude; c->coordinates=p->coordinates;
        }
        return;
    }
    if (c->wifi_count<PC_WIFI_MAX) {
        for (int i=c->wifi_count;i>0;i--) c->wifis[i]=c->wifis[i-1];
        c->wifi_count++;
    } else {
        for (int i=PC_WIFI_MAX-1;i>0;i--) c->wifis[i]=c->wifis[i-1];
    }
    pc_wifi_profile_t *p=&c->wifis[0];
    *p=(pc_wifi_profile_t){0};
    strcpy(p->ssid,c->ssid); strcpy(p->password,c->password);
    strcpy(p->city,c->city); strcpy(p->location,c->location);
    p->latitude=c->latitude; p->longitude=c->longitude; p->coordinates=c->coordinates;
}
bool pc_config_wifi_upsert(pc_config_t *c,const pc_wifi_profile_t *profile,bool make_active)
{
    if (!c || !profile || !profile->ssid[0] || strlen(profile->ssid)>=sizeof(profile->ssid)) return false;
    size_t pw=strlen(profile->password);
    if (pw && (pw<8 || pw>63)) return false;
    pc_wifi_profile_t item=*profile;
    if (!item.coordinates || !pc_location_valid(item.location) || !isfinite(item.latitude) || !isfinite(item.longitude) ||
        item.latitude<-90 || item.latitude>90 || item.longitude<-180 || item.longitude>180) {
        item.coordinates=false; item.latitude=0; item.longitude=0;
    }
    int idx=pc_config_wifi_find(c,item.ssid);
    if (idx>=0) {
        c->wifis[idx]=item;
    } else {
        if (c->wifi_count>=PC_WIFI_MAX) return false;
        idx=c->wifi_count++;
        c->wifis[idx]=item;
    }
    if (make_active || !c->ssid[0] || !strcmp(c->ssid,item.ssid)) {
        strcpy(c->ssid,item.ssid); strcpy(c->password,item.password);
        if (item.city[0]) {
            strcpy(c->city,item.city); strcpy(c->location,item.location);
            c->latitude=item.latitude; c->longitude=item.longitude; c->coordinates=item.coordinates;
        }
    }
    return true;
}
bool pc_config_wifi_activate(pc_config_t *c,const char *ssid)
{
    int idx=pc_config_wifi_find(c,ssid);
    if (idx<0) return false;
    const pc_wifi_profile_t *p=&c->wifis[idx];
    strcpy(c->ssid,p->ssid); strcpy(c->password,p->password);
    if (p->city[0]) {
        strcpy(c->city,p->city); strcpy(c->location,p->location);
        c->latitude=p->latitude; c->longitude=p->longitude; c->coordinates=p->coordinates;
    }
    return true;
}
bool pc_config_wifi_remove(pc_config_t *c,const char *ssid)
{
    int idx=pc_config_wifi_find(c,ssid);
    if (idx<0) return false;
    bool was_active=!strcmp(c->ssid,ssid);
    for (int i=idx;i+1<c->wifi_count;i++) c->wifis[i]=c->wifis[i+1];
    c->wifi_count--;
    if (c->wifi_count>=0 && c->wifi_count<PC_WIFI_MAX) memset(&c->wifis[c->wifi_count],0,sizeof(c->wifis[0]));
    if (was_active) {
        if (c->wifi_count>0) pc_config_wifi_activate(c,c->wifis[0].ssid);
        else { c->ssid[0]=0; c->password[0]=0; }
    }
    return true;
}
bool pc_config_from_json(const char *json,pc_config_t *out)
{
    cJSON *root=cJSON_Parse(json);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    pc_config_t c={0};
    pc_audio_defaults(&c.audio);
    const cJSON *wifi=cJSON_GetObjectItemCaseSensitive(root,"wifi");
    const cJSON *weather=cJSON_GetObjectItemCaseSensitive(root,"qweather");
    const cJSON *city=cJSON_GetObjectItemCaseSensitive(root,"city");
    const cJSON *audio=cJSON_GetObjectItemCaseSensitive(root,"audio");
    const cJSON *wifis=cJSON_GetObjectItemCaseSensitive(root,"wifis");
    optional_text(wifi,"ssid",c.ssid,sizeof(c.ssid)); optional_text(wifi,"password",c.password,sizeof(c.password));
    optional_text(weather,"host",c.host,sizeof(c.host)); optional_text(weather,"api_key",c.api_key,sizeof(c.api_key));
    optional_text(city,"name",c.city,sizeof(c.city)); optional_text(city,"location",c.location,sizeof(c.location));
    const cJSON *lat=cJSON_GetObjectItemCaseSensitive(city,"latitude"),*lon=cJSON_GetObjectItemCaseSensitive(city,"longitude");
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon) && pc_location_valid(c.location)) {
        c.latitude=lat->valuedouble; c.longitude=lon->valuedouble; c.coordinates=true;
    }
    if (cJSON_IsArray(wifis)) {
        const cJSON *item;
        cJSON_ArrayForEach(item,wifis) {
            if (!cJSON_IsObject(item) || c.wifi_count>=PC_WIFI_MAX) continue;
            pc_wifi_profile_t p={0};
            optional_text(item,"ssid",p.ssid,sizeof(p.ssid));
            optional_text(item,"password",p.password,sizeof(p.password));
            optional_text(item,"city",p.city,sizeof(p.city));
            optional_text(item,"location",p.location,sizeof(p.location));
            const cJSON *wlat=cJSON_GetObjectItemCaseSensitive(item,"latitude");
            const cJSON *wlon=cJSON_GetObjectItemCaseSensitive(item,"longitude");
            if (cJSON_IsNumber(wlat) && cJSON_IsNumber(wlon) && pc_location_valid(p.location)) {
                p.latitude=wlat->valuedouble; p.longitude=wlon->valuedouble; p.coordinates=true;
            }
            if (p.ssid[0]) pc_config_wifi_upsert(&c,&p,false);
        }
    }
    pc_config_sync_active_wifi(&c);
    if (cJSON_IsObject(audio)) {
        const cJSON *vol=cJSON_GetObjectItemCaseSensitive(audio,"volume");
        const cJSON *cs=cJSON_GetObjectItemCaseSensitive(audio,"chime_start");
        const cJSON *ce=cJSON_GetObjectItemCaseSensitive(audio,"chime_end");
        const cJSON *keys=cJSON_GetObjectItemCaseSensitive(audio,"keys");
        const cJSON *boot=cJSON_GetObjectItemCaseSensitive(audio,"boot");
        char buf[20];
        if (cJSON_IsNumber(vol) && vol->valueint>=10 && vol->valueint<=100) c.audio.volume=vol->valueint;
        if (cJSON_IsNumber(cs) && cs->valueint>=0 && cs->valueint<=23) c.audio.chime_start=cs->valueint;
        if (cJSON_IsNumber(ce) && ce->valueint>=0 && ce->valueint<=23) c.audio.chime_end=ce->valueint;
        if (cJSON_IsBool(keys)) c.audio.keys=cJSON_IsTrue(keys);
        if (cJSON_IsBool(boot)) c.audio.boot=cJSON_IsTrue(boot);
        if (text(audio,"chime",buf,sizeof(buf)) && pc_melody_valid(buf)) strcpy(c.audio.chime,buf);
        if (text(audio,"anniv",buf,sizeof(buf)) && pc_melody_valid(buf)) strcpy(c.audio.anniv,buf);
    }
    cJSON_Delete(root);
    *out=c; return true;
}
char *pc_config_to_json(const pc_config_t *in)
{
    pc_config_t local=*in;
    pc_config_sync_active_wifi(&local);
    const pc_config_t *c=&local;
    cJSON *root=cJSON_CreateObject(),*wifi=cJSON_AddObjectToObject(root,"wifi");
    cJSON *weather=cJSON_AddObjectToObject(root,"qweather"),*city=cJSON_AddObjectToObject(root,"city");
    cJSON *audio=cJSON_AddObjectToObject(root,"audio"),*wifis=cJSON_AddArrayToObject(root,"wifis");
    if (!city || !audio || !wifis) { cJSON_Delete(root); return NULL; }
    cJSON_AddNumberToObject(root,"version",1);
    cJSON_AddStringToObject(wifi,"ssid",c->ssid); cJSON_AddStringToObject(wifi,"password",c->password);
    cJSON_AddStringToObject(weather,"host",c->host); cJSON_AddStringToObject(weather,"api_key",c->api_key);
    cJSON_AddStringToObject(city,"name",c->city); cJSON_AddStringToObject(city,"location",c->location);
    if (c->coordinates) {
        cJSON_AddNumberToObject(city,"latitude",c->latitude); cJSON_AddNumberToObject(city,"longitude",c->longitude);
    }
    for (int i=0;i<c->wifi_count && i<PC_WIFI_MAX;i++) {
        const pc_wifi_profile_t *p=&c->wifis[i];
        if (!p->ssid[0]) continue;
        cJSON *w=cJSON_CreateObject();
        if (!w) continue;
        cJSON_AddStringToObject(w,"ssid",p->ssid);
        cJSON_AddStringToObject(w,"password",p->password);
        cJSON_AddStringToObject(w,"city",p->city);
        cJSON_AddStringToObject(w,"location",p->location);
        if (p->coordinates) {
            cJSON_AddNumberToObject(w,"latitude",p->latitude);
            cJSON_AddNumberToObject(w,"longitude",p->longitude);
        }
        cJSON_AddItemToArray(wifis,w);
    }
    pc_audio_config_t a=c->audio;
    if (a.volume<10 || a.volume>100) a.volume=60;
    if (!pc_melody_valid(a.chime)) strcpy(a.chime,"off");
    if (!pc_melody_valid(a.anniv)) strcpy(a.anniv,"off");
    if (a.chime_start<0 || a.chime_start>23) a.chime_start=8;
    if (a.chime_end<0 || a.chime_end>23) a.chime_end=21;
    cJSON_AddNumberToObject(audio,"volume",a.volume);
    cJSON_AddBoolToObject(audio,"keys",a.keys);
    cJSON_AddBoolToObject(audio,"boot",a.boot);
    cJSON_AddStringToObject(audio,"chime",a.chime);
    cJSON_AddNumberToObject(audio,"chime_start",a.chime_start);
    cJSON_AddNumberToObject(audio,"chime_end",a.chime_end);
    cJSON_AddStringToObject(audio,"anniv",a.anniv);
    char *json=cJSON_Print(root); cJSON_Delete(root); return json;
}
bool pc_city_parse(const char *body,pc_config_t *config)
{
    cJSON *root=cJSON_Parse(body);
    const cJSON *locations=cJSON_GetObjectItemCaseSensitive(root,"location"),*item;
    bool ok=false;
    if (success(root)) cJSON_ArrayForEach(item,locations) {
        char id[33],lat[32],lon[32],zone[48],name[97],*end;
        if (!text(item,"id",id,sizeof(id)) || !pc_location_valid(id) || !text(item,"lat",lat,sizeof(lat)) ||
            !text(item,"lon",lon,sizeof(lon)) || !text(item,"name",name,sizeof(name)) ||
            !text(item,"tz",zone,sizeof(zone)) || strcmp(zone,"Asia/Shanghai")) continue;
        double a=strtod(lat,&end); if (*end) continue;
        double b=strtod(lon,&end); if (*end) continue;
        strcpy(config->location,id); strcpy(config->city,name);
        config->latitude=a; config->longitude=b; config->coordinates=true;
        pc_config_sync_active_wifi(config);
        ok=true; break;
    }
    cJSON_Delete(root); return ok;
}
uint32_t pc_frame_hash(const uint8_t *pixels,size_t length)
{
    uint32_t hash=2166136261u;
    for (size_t i=0;i<length;i++) hash=(hash ^ pixels[i])*16777619u;
    return hash;
}
