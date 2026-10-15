#include "services.h"
#include "render.h"
#include "sdcard.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

extern const uint8_t page_start[] __asm__("_binary_index_html_start");
extern const uint8_t page_end[] __asm__("_binary_index_html_end");
extern const uint8_t app_start[] __asm__("_binary_app_js_start");
extern const uint8_t app_end[] __asm__("_binary_app_js_end");
extern const uint8_t wasm_start[] __asm__("_binary_e6_wasm_start");
extern const uint8_t wasm_end[] __asm__("_binary_e6_wasm_end");
extern esp_err_t pc_network_ap_start(void);
static httpd_handle_t server;
static pc_config_t *active,draft;
static esp_netif_t *ap_netif;
static atomic_llong activity,finished;
static cJSON *candidates;
static portMUX_TYPE preview_lock=portMUX_INITIALIZER_UNLOCKED;
static char preview_id[17];
static const char *NO_CARD="未检测到 TF 卡：配置、纪念日和图片都保存在卡上，请插卡后重启";

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
static esp_err_t app_get(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req,"text/javascript; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char *)app_start,app_end-app_start-1);
}
static esp_err_t wasm_get(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req,"application/wasm");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,(const char *)wasm_start,wasm_end-wasm_start);
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root);
static esp_err_t status_get(httpd_req_t *req)
{
    touch();
    cJSON *root=cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root,"ssid",active->ssid);
    cJSON_AddStringToObject(root,"host",active->host); cJSON_AddStringToObject(root,"city",active->city);
    cJSON_AddStringToObject(root,"location",active->location);
    cJSON_AddBoolToObject(root,"has_key",active->api_key[0]); cJSON_AddBoolToObject(root,"has_password",active->password[0]);
    cJSON_AddBoolToObject(root,"card",pc_storage_card());
    pc_device_status_t st; pc_device_status(&st);
    cJSON_AddBoolToObject(root,"time_valid",st.time_valid);
    cJSON_AddNumberToObject(root,"now",(double)time(NULL));
    cJSON_AddNumberToObject(root,"battery",st.battery_percent);
    cJSON_AddStringToObject(root,"weather",st.weather_valid?(st.weather_stale?"stale":"fresh"):"none");
    cJSON_AddNumberToObject(root,"weather_fetched",(double)st.weather_fetched);
    cJSON_AddBoolToObject(root,"offline",st.offline);
    cJSON_AddNumberToObject(root,"holiday_first",st.holiday_first);
    cJSON_AddNumberToObject(root,"holiday_last",st.holiday_last);
    cJSON_AddNumberToObject(root,"anniversaries",st.anniversaries);
    cJSON_AddStringToObject(root,"page",st.anniversary_page?"anniversary":"calendar");
    cJSON_AddBoolToObject(root,"celebrate_today",st.celebrate_today);
    cJSON *fonts=cJSON_AddArrayToObject(root,"fonts");
    static const int SIZES[]={18,24,36};
    for (int i=0;i<3;i++) if (st.fonts[i]) cJSON_AddItemToArray(fonts,cJSON_CreateNumber(SIZES[i]));
    struct stat file;
    cJSON_AddBoolToObject(root,"onnx",pc_storage_card() && !stat(PC_SD_DIR "/web/ort/manifest.json",&file));
    return send_json(req,root);
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
    if (!pc_storage_card()) return reply(req,false,NO_CARD);
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
    if (!pc_storage_card()) return reply(req,false,NO_CARD);
    char *json=receive(req,PC_HOLIDAY_MAX_JSON),error[96];
    if (!json) return reply(req,false,"文件为空、过大或接收失败");
    pc_holidays_t *table=pc_holidays_parse(json,req->content_len,error,sizeof(error));
    if (!table) { free(json); return reply(req,false,error); }
    pc_holidays_free(table);
    esp_err_t err=pc_holidays_save(json,req->content_len); free(json);
    if (err==ESP_OK) pc_preview_reload(PC_RELOAD_HOLIDAYS);
    return reply(req,err==ESP_OK,err==ESP_OK?"年度表已保存并生效":"保存失败，旧年度表保留");
}
static esp_err_t exit_post(httpd_req_t *req)
{
    touch();
    esp_err_t err=reply(req,true,"正在退出配置"); atomic_store(&finished,esp_timer_get_time()); return err;
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root)
{
    char *body=root?cJSON_PrintUnformatted(root):NULL; cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req,"application/json; charset=utf-8"); httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN); free(body); return err;
}
static esp_err_t anniversaries_get(httpd_req_t *req)
{
    touch();
    pc_anniversaries_t *list=malloc(sizeof(*list));
    if (!list) return ESP_ERR_NO_MEM;
    pc_anniversaries_load(list);
    char *items=pc_anniversaries_json(list); free(list);
    cJSON *root=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"card",pc_storage_card());
    cJSON_AddNumberToObject(root,"max",PC_ANNIV_MAX);
    cJSON_AddItemToObject(root,"items",items?cJSON_Parse(items):cJSON_CreateArray());
    free(items);
    return send_json(req,root);
}
static bool image_exists(const char *id)
{
    char path[96]; struct stat st;
    return pc_image_path(id,"e6",path,sizeof(path)) && !stat(path,&st) && st.st_size==PC_PACKED_BYTES;
}
static esp_err_t anniversaries_post(httpd_req_t *req)
{
    touch();
    if (!pc_storage_card()) return reply(req,false,NO_CARD);
    char *body=receive(req,2048),error[96]; pc_anniversary_t item;
    bool received=body!=NULL, ok=received && pc_anniversary_parse_item(body,&item,error,sizeof(error));
    free(body);
    if (!ok) return reply(req,false,received?error:"请求为空或过大");
    if (item.image && !image_exists(item.id)) item.image=false;
    pc_anniversaries_t *list=malloc(sizeof(*list));
    if (!list) return reply(req,false,"内存不足");
    pc_anniversaries_load(list);
    ok=pc_anniversaries_upsert(list,&item);
    esp_err_t err=ok?pc_anniversaries_save(list):ESP_FAIL;
    free(list);
    if (!ok) return reply(req,false,"纪念日数量已达上限");
    if (err!=ESP_OK) return reply(req,false,"写入 TF 卡失败");
    if (!item.image) pc_image_remove(item.id);
    pc_preview_reload(PC_RELOAD_ANNIVERSARIES);
    char missing[97];
    size_t n=pc_render_missing(item.name,36,missing,sizeof(missing));
    cJSON *root=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"ok",true);
    cJSON_AddStringToObject(root,"message",n?"已保存；部分字符缺字，屏幕上显示为 ?":"已保存，可在屏幕上预览");
    cJSON_AddStringToObject(root,"missing",missing);
    cJSON_AddBoolToObject(root,"image",item.image);
    return send_json(req,root);
}
static esp_err_t anniversaries_delete(httpd_req_t *req)
{
    touch();
    if (!pc_storage_card()) return reply(req,false,NO_CARD);
    cJSON *root=request_json(req); char id[17];
    bool ok=field(root,"id",id,sizeof(id)) && pc_anniversary_id_valid(id);
    cJSON_Delete(root);
    if (!ok) return reply(req,false,"纪念日 ID 无效");
    pc_anniversaries_t *list=malloc(sizeof(*list));
    if (!list) return reply(req,false,"内存不足");
    pc_anniversaries_load(list);
    ok=pc_anniversaries_remove(list,id);
    esp_err_t err=ok?pc_anniversaries_save(list):ESP_OK;
    free(list);
    if (err!=ESP_OK) return reply(req,false,"写入 TF 卡失败");
    pc_image_remove(id);
    pc_preview_reload(PC_RELOAD_ANNIVERSARIES);
    return reply(req,true,ok?"已删除":"纪念日不存在");
}
static esp_err_t preview_post(httpd_req_t *req)
{
    touch();
    cJSON *root=request_json(req); char id[17];
    bool ok=field(root,"id",id,sizeof(id)) && pc_anniversary_id_valid(id);
    cJSON_Delete(root);
    if (!ok) return reply(req,false,"纪念日 ID 无效");
    portENTER_CRITICAL(&preview_lock); strcpy(preview_id,id); portEXIT_CRITICAL(&preview_lock);
    return reply(req,true,"屏幕正在刷新，约需 15–30 秒");
}
/* The server has one task, so these PSRAM buffers are never used by two requests at once. */
static uint8_t *render_pixels,*render_frame,*render_body;
static bool render_buffers(void)
{
    const uint32_t caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    if (!render_pixels) render_pixels=heap_caps_malloc(PC_PIXELS,caps);
    if (!render_frame) render_frame=heap_caps_malloc(PC_PACKED_BYTES,caps);
    if (!render_body) render_body=heap_caps_malloc(PC_DRAFT_MAX_BODY,caps);
    return render_pixels && render_frame && render_body;
}
static esp_err_t send_frame(httpd_req_t *req,const pc_preview_t *request)
{
    pc_preview_render(request,render_pixels);
    pc_packed_pack(render_pixels,render_frame);
    pc_device_status_t st; pc_device_status(&st);
    char status[96];
    snprintf(status,sizeof(status),"time=%d,weather=%s,card=%d,battery=%d",st.time_valid,
             st.weather_valid?(st.weather_stale?"stale":"fresh"):"none",st.card,st.battery_percent);
    httpd_resp_set_type(req,"application/octet-stream"); httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Status",status);
    return httpd_resp_send(req,(const char *)render_frame,PC_PACKED_BYTES);
}
static esp_err_t render_get(httpd_req_t *req)
{
    touch();
    if (!render_buffers()) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no memory");
    pc_preview_t request={.kind=PC_PREVIEW_CALENDAR};
    char query[96],page[16]="calendar",value[24];
    if (httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK) {
        httpd_query_key_value(query,"page",page,sizeof(page));
        if (!strcmp(page,"anniversary")) {
            request.kind=PC_PREVIEW_ANNIVERSARY;
            if (httpd_query_key_value(query,"id",value,sizeof(value))==ESP_OK && pc_anniversary_id_valid(value))
                strcpy(request.id,value);
        } else if (strcmp(page,"calendar")) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"page");
        else if (httpd_query_key_value(query,"month",value,sizeof(value))==ESP_OK) {
            /* YYYY-MM */
            if (sscanf(value,"%4d-%2d",&request.year,&request.month)!=2 || request.year<2000 || request.year>2099 ||
                request.month<1 || request.month>12)
                return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"month");
        }
    }
    return send_frame(req,&request);
}
static esp_err_t render_post(httpd_req_t *req)
{
    touch();
    if (!render_buffers()) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"no memory");
    if (!req->content_len || req->content_len>PC_DRAFT_MAX_BODY) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"size");
    size_t at=0;
    while (at<req->content_len) {
        int n=httpd_req_recv(req,(char *)render_body+at,req->content_len-at);
        if (n<=0) return ESP_FAIL;
        at+=n; touch();
    }
    pc_anniversary_t item; char error[96];
    pc_preview_t request={.kind=PC_PREVIEW_DRAFT,.draft=&item};
    if (!pc_draft_parse(render_body,at,&item,&request.draft_image,error,sizeof(error)))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,error);
    return send_frame(req,&request);
}
bool pc_setup_preview(char id[17])
{
    portENTER_CRITICAL(&preview_lock);
    bool ready=preview_id[0]!=0;
    if (ready) { strcpy(id,preview_id); preview_id[0]=0; }
    portEXIT_CRITICAL(&preview_lock);
    return ready;
}
static bool image_query(httpd_req_t *req,char *path,size_t capacity,char kind[8])
{
    char query[96],id[24];
    return httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK &&
        httpd_query_key_value(query,"id",id,sizeof(id))==ESP_OK &&
        httpd_query_key_value(query,"kind",kind,8)==ESP_OK && pc_image_path(id,kind,path,capacity);
}
static const char *mime(const char *path)
{
    const char *dot=strrchr(path,'.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot,".js") || !strcmp(dot,".mjs")) return "text/javascript";
    if (!strcmp(dot,".wasm")) return "application/wasm";
    if (!strcmp(dot,".json")) return "application/json; charset=utf-8";
    if (!strcmp(dot,".jpg")) return "image/jpeg";
    return "application/octet-stream";
}
static esp_err_t send_file(httpd_req_t *req,const char *path,const char *cache)
{
    FILE *f=fopen(path,"rb");
    if (!f) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"not found");
    char *chunk=malloc(4096);
    if (!chunk) { fclose(f); return ESP_ERR_NO_MEM; }
    httpd_resp_set_type(req,mime(path)); httpd_resp_set_hdr(req,"Cache-Control",cache);
    esp_err_t err=ESP_OK; size_t n;
    while (err==ESP_OK && (n=fread(chunk,1,4096,f))>0) { touch(); err=httpd_resp_send_chunk(req,chunk,n); }
    fclose(f); free(chunk);
    return err==ESP_OK ? httpd_resp_send_chunk(req,NULL,0) : err;
}
static esp_err_t image_get(httpd_req_t *req)
{
    touch();
    char path[96],kind[8];
    if (!pc_storage_card() || !image_query(req,path,sizeof(path),kind)) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"not found");
    return send_file(req,path,"no-store");
}
static esp_err_t image_post(httpd_req_t *req)
{
    touch();
    if (!pc_storage_card()) return reply(req,false,NO_CARD);
    char path[96],tmp[104],kind[8];
    if (!image_query(req,path,sizeof(path),kind)) return reply(req,false,"图片参数无效");
    size_t limit=!strcmp(kind,"e6")?PC_PACKED_BYTES:!strcmp(kind,"jpg")?1600000:8192;
    if (!req->content_len || req->content_len>limit || (!strcmp(kind,"e6") && req->content_len!=PC_PACKED_BYTES))
        return reply(req,false,"图片大小不正确");
    snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=fopen(tmp,"wb");
    if (!f) return reply(req,false,"无法写入 TF 卡");
    uint8_t *chunk=malloc(4096); size_t at=0; bool ok=chunk!=NULL;
    while (ok && at<req->content_len) {
        int n=httpd_req_recv(req,(char *)chunk,req->content_len-at<4096?req->content_len-at:4096);
        if (n<=0) { ok=false; break; }
        touch();
        if (!strcmp(kind,"e6")) for (int i=0;i<n && ok;i++) ok=(chunk[i]>>4)<6 && (chunk[i]&15)<6;
        ok=ok && fwrite(chunk,1,n,f)==(size_t)n;
        at+=n;
    }
    ok=ok && fflush(f)==0 && fsync(fileno(f))==0;
    ok=fclose(f)==0 && ok; free(chunk);
    if (!ok) { unlink(tmp); return reply(req,false,"接收或写入失败，请重试"); }
    return reply(req,pc_file_commit(tmp,path)==ESP_OK,"已写入 TF 卡");
}
/* Optional browser assets (ONNX Runtime, models) copied to /papercolor/web/ on the card. */
static esp_err_t sdweb_get(httpd_req_t *req)
{
    touch();
    const char *rest=req->uri+strlen("/sdweb/");
    size_t n=strcspn(rest,"?");
    char path[160];
    bool ok=pc_storage_card() && n && n<64 && !strstr(rest,"..");
    for (size_t i=0;ok && i<n;i++) {
        char c=rest[i];
        ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c=='/';
    }
    if (!ok) return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"not found");
    snprintf(path,sizeof(path),PC_SD_DIR "/web/%.*s",(int)n,rest);
    return send_file(req,path,strstr(path,"manifest.json")?"no-store":"public, max-age=31536000, immutable");
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
    active=config; draft=*config; cJSON_Delete(candidates); candidates=NULL; preview_id[0]=0;
    atomic_store(&finished,0); atomic_store(&activity,esp_timer_get_time());
    if (!ap_netif) ap_netif=esp_netif_create_default_wifi_ap();
    if (!ap_netif) return ESP_ERR_NO_MEM;
    esp_err_t err=pc_network_ap_start(); if (err!=ESP_OK) return err;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=12288; cfg.recv_wait_timeout=10; cfg.send_wait_timeout=10; cfg.max_uri_handlers=20;
    cfg.uri_match_fn=httpd_uri_match_wildcard; cfg.lru_purge_enable=true;
    err=httpd_start(&server,&cfg); if (err!=ESP_OK) return err;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_get}, {.uri="/app.js",.method=HTTP_GET,.handler=app_get},
        {.uri="/e6.wasm",.method=HTTP_GET,.handler=wasm_get}, {.uri="/api/status",.method=HTTP_GET,.handler=status_get},
        {.uri="/api/connect",.method=HTTP_POST,.handler=connect_post}, {.uri="/api/cities",.method=HTTP_POST,.handler=city_post},
        {.uri="/api/save",.method=HTTP_POST,.handler=save_post}, {.uri="/api/holidays",.method=HTTP_POST,.handler=holidays_post},
        {.uri="/api/exit",.method=HTTP_POST,.handler=exit_post},
        {.uri="/api/anniversaries",.method=HTTP_GET,.handler=anniversaries_get},
        {.uri="/api/anniversaries",.method=HTTP_POST,.handler=anniversaries_post},
        {.uri="/api/anniversaries/delete",.method=HTTP_POST,.handler=anniversaries_delete},
        {.uri="/api/preview",.method=HTTP_POST,.handler=preview_post},
        {.uri="/api/render",.method=HTTP_GET,.handler=render_get}, {.uri="/api/render",.method=HTTP_POST,.handler=render_post},
        {.uri="/api/image",.method=HTTP_GET,.handler=image_get}, {.uri="/api/image",.method=HTTP_POST,.handler=image_post},
        {.uri="/sdweb/*",.method=HTTP_GET,.handler=sdweb_get},
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
