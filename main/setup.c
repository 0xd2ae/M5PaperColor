#include "services.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

extern const uint8_t page_start[] __asm__("_binary_index_html_start");
extern const uint8_t page_end[] __asm__("_binary_index_html_end");
extern esp_err_t pc_network_ap_start(void);
static httpd_handle_t server;
static pc_config_t *active,draft;
static esp_netif_t *ap_netif;
static atomic_llong activity,finished;
static cJSON *candidates;

static void touch(void)
{
    atomic_store(&activity,esp_timer_get_time());
}
static esp_err_t reply(httpd_req_t *req,bool ok,const char *message)
{
    cJSON *root=cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root,"ok",ok); cJSON_AddStringToObject(root,"message",message);
    char *body=cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8"); httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN); free(body); return err;
}
static char *receive(httpd_req_t *req,size_t maximum)
{
    if (!req->content_len || req->content_len>maximum) return NULL;
    char *body=malloc(req->content_len+1); if (!body) return NULL;
    size_t at=0;
    while (at<req->content_len) {
        int n=httpd_req_recv(req,body+at,req->content_len-at);
        if (n<=0) { free(body); return NULL; } at+=n;
    }
    body[at]=0; return body;
}
static cJSON *request_json(httpd_req_t *req)
{
    char *body=receive(req,2048); if (!body) return NULL;
    const char *end; cJSON *root=cJSON_ParseWithOpts(body,&end,true); free(body); return root;
}
static bool field(const cJSON *root,const char *key,char *out,size_t size)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,key);
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring)>=size) return false;
    strcpy(out,v->valuestring); return true;
}
static bool api_fields(const cJSON *root,pc_config_t *config)
{
    if (!field(root,"host",config->host,sizeof(config->host)) || !pc_host_valid(config->host)) return false;
    char key[129]; if (!field(root,"api_key",key,sizeof(key))) return false;
    if (*key) strcpy(config->api_key,key);
    else {
        if (strcmp(config->host,active->host)) return false;
        strcpy(config->api_key,active->api_key);
    }
    if (!config->api_key[0]) return false;
    for (const char *p=config->api_key;*p;p++) if (!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='-'||*p=='_')) return false;
    return true;
}
static esp_err_t index_get(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char *)page_start,page_end-page_start-1);
}
static esp_err_t status_get(httpd_req_t *req)
{
    touch();
    cJSON *root=cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root,"ssid",active->ssid);
    cJSON_AddStringToObject(root,"host",active->host); cJSON_AddStringToObject(root,"city",active->city);
    cJSON_AddStringToObject(root,"location",active->location);
    cJSON_AddBoolToObject(root,"has_key",active->api_key[0]); cJSON_AddBoolToObject(root,"has_password",active->password[0]);
    char *body=cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8"); httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN); free(body); return err;
}
static esp_err_t connect_post(httpd_req_t *req)
{
    touch();
    cJSON *root=request_json(req); pc_config_t candidate=draft;
    char password[65]; bool ok=field(root,"ssid",candidate.ssid,sizeof(candidate.ssid)) && candidate.ssid[0] && field(root,"password",password,sizeof(password));
    if (ok) {
        if (password[0]) strcpy(candidate.password,password);
        else if (!strcmp(candidate.ssid,active->ssid)) strcpy(candidate.password,active->password);
        else candidate.password[0]=0;
        size_t n=strlen(candidate.password); ok=!n || (n>=8 && n<=63);
    }
    cJSON_Delete(root);
    if (!ok) return reply(req,false,"Wi-Fi 名称或密码格式错误");
    esp_err_t err=pc_wifi_connect(&candidate,esp_timer_get_time()+20000000);
    if (err==ESP_OK) {
        draft=candidate;
        if (pc_sync_time(esp_timer_get_time()+10000000)) { /* app persists RTC after mode exits */ }
    }
    return reply(req,err==ESP_OK,err==ESP_OK?"Wi-Fi 已连接，可以搜索城市":"连接失败，请检查 Wi-Fi；原配置未修改");
}
static bool url_encode(const char *input,char *out,size_t capacity)
{
    static const char hex[]="0123456789ABCDEF"; size_t n=0;
    for (const unsigned char *p=(const unsigned char *)input;*p;p++) {
        if (n+3>=capacity) return false;
        out[n++]='%'; out[n++]=hex[*p>>4]; out[n++]=hex[*p&15];
    }
    out[n]=0; return true;
}
static esp_err_t city_post(httpd_req_t *req)
{
    touch();
    cJSON *root=request_json(req); pc_config_t candidate=draft;
    char query[97],encoded[300],path[420];
    bool ok=api_fields(root,&candidate) && field(root,"query",query,sizeof(query)) && query[0] && url_encode(query,encoded,sizeof(encoded));
    cJSON_Delete(root);
    if (!ok) return reply(req,false,"请填写 API Host、凭据和城市名称");
    snprintf(path,sizeof(path),"/geo/v2/city/lookup?location=%s&range=cn&number=10&lang=zh",encoded);
    char *body=NULL; esp_err_t err=pc_http_get(&candidate,path,esp_timer_get_time()+20000000,&body);
    cJSON *result=body?cJSON_Parse(body):NULL;
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(result,"code"), *locations=cJSON_GetObjectItemCaseSensitive(result,"location");
    if (err!=ESP_OK || !cJSON_IsString(code) || strcmp(code->valuestring,"200") || !cJSON_IsArray(locations) || !cJSON_GetArraySize(locations)) {
        free(body); cJSON_Delete(result); return reply(req,false,"城市查询失败，请先连接 Wi-Fi，并核对 API Host 和凭据");
    }
    cJSON_Delete(candidates); candidates=cJSON_Duplicate(locations,true); draft=candidate;
    cJSON_Delete(result);
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    err=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN); free(body); return err;
}
static esp_err_t save_post(httpd_req_t *req)
{
    touch();
    cJSON *root=request_json(req); pc_config_t candidate=draft;
    char id[33]; bool ok=api_fields(root,&candidate) && field(root,"location",id,sizeof(id));
    cJSON_Delete(root);
    if (!ok) return reply(req,false,"配置格式错误");
    if (strcmp(id,active->location) || !active->coordinates || candidates) {
        const cJSON *choice=NULL,*item;
        cJSON_ArrayForEach(item,candidates) {
            const cJSON *value=cJSON_GetObjectItemCaseSensitive(item,"id");
            if (cJSON_IsString(value) && !strcmp(value->valuestring,id)) { choice=item; break; }
        }
        char lat[32],lon[32],zone[48],name[97];
        if (!choice || !field(choice,"lat",lat,sizeof(lat)) || !field(choice,"lon",lon,sizeof(lon)) ||
            !field(choice,"name",name,sizeof(name)) || !field(choice,"tz",zone,sizeof(zone)) || strcmp(zone,"Asia/Shanghai"))
            return reply(req,false,"请从查询结果中选择中国大陆城市");
        char *end; candidate.latitude=strtod(lat,&end); if (*end) return reply(req,false,"纬度格式错误");
        candidate.longitude=strtod(lon,&end); if (*end) return reply(req,false,"经度格式错误");
        strcpy(candidate.city,name); strcpy(candidate.location,id); candidate.coordinates=true;
    } else {
        strcpy(candidate.city,active->city); strcpy(candidate.location,active->location);
        candidate.latitude=active->latitude; candidate.longitude=active->longitude; candidate.coordinates=active->coordinates;
    }
    if (!pc_config_valid(&candidate)) return reply(req,false,"配置不完整，请连接 Wi-Fi 并选择城市");
    pc_weather_t weather={0};
    if (pc_weather_fetch(&candidate,&weather,esp_timer_get_time()+25000000)!=ESP_OK)
        return reply(req,false,"天气接口验证失败，原配置未修改");
    if (pc_config_save(&candidate)!=ESP_OK) return reply(req,false,"保存失败，原配置未修改");
    *active=candidate; pc_weather_save(&weather);
    esp_err_t err=reply(req,true,"已保存，即将退出配置并更新屏幕");
    atomic_store(&finished,esp_timer_get_time()); return err;
}
static esp_err_t holidays_post(httpd_req_t *req)
{
    touch();
    char *json=receive(req,PC_HOLIDAY_MAX_JSON),error[96];
    if (!json) return reply(req,false,"文件为空、过大或接收失败");
    pc_holidays_t *table=pc_holidays_parse(json,req->content_len,error,sizeof(error));
    if (!table) { free(json); return reply(req,false,error); }
    pc_holidays_free(table);
    esp_err_t err=pc_holidays_save(json,req->content_len); free(json);
    return reply(req,err==ESP_OK,err==ESP_OK?"年度表已保存，退出配置后生效":"保存失败，旧年度表保留");
}
static esp_err_t exit_post(httpd_req_t *req)
{
    touch();
    esp_err_t err=reply(req,true,"正在退出配置"); atomic_store(&finished,esp_timer_get_time()); return err;
}
static esp_err_t captive_get(httpd_req_t *req)
{
    touch();
    httpd_resp_set_status(req,"302 Found");
    httpd_resp_set_hdr(req,"Location","/");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,NULL,0);
}
esp_err_t pc_setup_start(pc_config_t *config)
{
    active=config; draft=*config; cJSON_Delete(candidates); candidates=NULL;
    atomic_store(&finished,0); atomic_store(&activity,esp_timer_get_time());
    if (!ap_netif) ap_netif=esp_netif_create_default_wifi_ap();
    if (!ap_netif) return ESP_ERR_NO_MEM;
    esp_err_t err=pc_network_ap_start(); if (err!=ESP_OK) return err;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=12288; cfg.recv_wait_timeout=10; cfg.send_wait_timeout=10; cfg.max_uri_handlers=9;
    cfg.uri_match_fn=httpd_uri_match_wildcard;
    err=httpd_start(&server,&cfg); if (err!=ESP_OK) return err;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_get}, {.uri="/api/status",.method=HTTP_GET,.handler=status_get},
        {.uri="/api/connect",.method=HTTP_POST,.handler=connect_post}, {.uri="/api/cities",.method=HTTP_POST,.handler=city_post},
        {.uri="/api/save",.method=HTTP_POST,.handler=save_post}, {.uri="/api/holidays",.method=HTTP_POST,.handler=holidays_post},
        {.uri="/api/exit",.method=HTTP_POST,.handler=exit_post},
        {.uri="/*",.method=HTTP_GET,.handler=captive_get}};
    for (size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        err=httpd_register_uri_handler(server,&routes[i]); if (err!=ESP_OK) { pc_setup_stop(); return err; }
    }
    return ESP_OK;
}
void pc_setup_stop(void)
{
    if (server) httpd_stop(server);
    server=NULL;
    cJSON_Delete(candidates); candidates=NULL; pc_wifi_off();
    esp_wifi_set_mode(WIFI_MODE_STA);
}
bool pc_setup_finished(void)
{
    int64_t when=atomic_load(&finished); return when && esp_timer_get_time()-when>2000000;
}
int64_t pc_setup_activity(void) { return atomic_load(&activity); }
