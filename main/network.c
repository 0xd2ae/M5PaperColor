#include "services.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "compression.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static EventGroupHandle_t events;
static esp_netif_t *station;
static bool started;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg; (void)data;
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(events,1);
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) xEventGroupClearBits(events,1);
}
esp_err_t pc_wifi_init(void)
{
    esp_err_t err=esp_netif_init(); if (err!=ESP_OK) return err;
    err=esp_event_loop_create_default(); if (err!=ESP_OK) return err;
    events=xEventGroupCreate(); if (!events) return ESP_ERR_NO_MEM;
    station=esp_netif_create_default_wifi_sta(); if (!station) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init); if (err!=ESP_OK) return err;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err!=ESP_OK) return err;
    err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL); if (err!=ESP_OK) return err;
    return esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL);
}
static int remaining(int64_t deadline)
{
    int64_t ms=(deadline-esp_timer_get_time())/1000;
    return ms<=0 ? 0 : ms>10000 ? 10000 : (int)ms;
}
int pc_wifi_scan(pc_wifi_ap_t *out,int max_count)
{
    if (!out || max_count<=0) return 0;
    wifi_mode_t mode=WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (mode!=WIFI_MODE_APSTA && mode!=WIFI_MODE_STA) {
        if (esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK) return 0;
    }
    if (!started) {
        if (esp_wifi_start()!=ESP_OK) return 0;
        started=true;
    }
    if (!(xEventGroupGetBits(events)&1)) esp_wifi_disconnect();
    wifi_scan_config_t cfg={
        .show_hidden=false,
        .scan_type=WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min=45,
        .scan_time.active.max=110,
    };
    if (esp_wifi_scan_start(&cfg,true)!=ESP_OK) return 0;
    uint16_t num=32;
    wifi_ap_record_t *recs=calloc(num,sizeof(*recs));
    if (!recs) { esp_wifi_clear_ap_list(); return 0; }
    if (esp_wifi_scan_get_ap_records(&num,recs)!=ESP_OK) { free(recs); return 0; }
    int count=0;
    for (uint16_t i=0;i<num;i++) {
        const char *ssid=(const char *)recs[i].ssid;
        if (!ssid[0] || !strcmp(ssid,"PaperColor-Setup")) continue;
        int found=-1;
        for (int j=0;j<count;j++) {
            if (!strcmp(out[j].ssid,ssid)) { found=j; break; }
        }
        bool auth=recs[i].authmode!=WIFI_AUTH_OPEN;
        if (found>=0) {
            if (recs[i].rssi>out[found].rssi) { out[found].rssi=recs[i].rssi; out[found].auth=auth; }
        } else if (count<max_count) {
            snprintf(out[count].ssid,sizeof(out[count].ssid),"%s",ssid);
            out[count].rssi=recs[i].rssi;
            out[count].auth=auth;
            count++;
        }
    }
    free(recs);
    for (int i=0;i<count;i++) {
        for (int j=i+1;j<count;j++) {
            if (out[j].rssi>out[i].rssi) {
                pc_wifi_ap_t tmp=out[i]; out[i]=out[j]; out[j]=tmp;
            }
        }
    }
    return count;
}
static esp_err_t connect_single(const char *ssid,const char *password,int64_t deadline)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    if (started) { esp_wifi_disconnect(); xEventGroupClearBits(events,1); }
    wifi_config_t wifi={0};
    memcpy(wifi.sta.ssid,ssid,strlen(ssid));
    if (password && password[0]) memcpy(wifi.sta.password,password,strlen(password));
    wifi.sta.threshold.authmode=(password && password[0])?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    wifi_mode_t mode=WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    esp_err_t err=esp_wifi_set_mode(mode==WIFI_MODE_APSTA?WIFI_MODE_APSTA:WIFI_MODE_STA); if (err!=ESP_OK) return err;
    err=esp_wifi_set_config(WIFI_IF_STA,&wifi); if (err!=ESP_OK) return err;
    if (!started) { err=esp_wifi_start(); if (err!=ESP_OK) return err; started=true; }
    err=esp_wifi_connect(); if (err!=ESP_OK) return err;
    while (remaining(deadline)) {
        if (xEventGroupWaitBits(events,1,pdFALSE,pdFALSE,pdMS_TO_TICKS(250))&1) return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}
esp_err_t pc_wifi_connect(pc_config_t *c,int64_t deadline)
{
    wifi_mode_t mode=WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    /* In APSTA setup mode or when only a single network is configured, connect directly to c->ssid. */
    if (mode==WIFI_MODE_APSTA || c->wifi_count<=1) return connect_single(c->ssid,c->password,deadline);
    pc_wifi_ap_t aps[24];
    int n_aps=pc_wifi_scan(aps,24);
    int order[PC_WIFI_MAX],n_order=0;
    bool queued[PC_WIFI_MAX]={0};
    /* First try saved networks visible in the scan, strongest signal first. */
    for (int a=0;a<n_aps;a++) {
        int idx=pc_config_wifi_find(c,aps[a].ssid);
        if (idx>=0 && !queued[idx]) { order[n_order++]=idx; queued[idx]=true; }
    }
    /* Prefer the currently active network next (handles hidden SSIDs), then any remaining saved profiles. */
    int active_idx=pc_config_wifi_find(c,c->ssid);
    if (active_idx>=0 && !queued[active_idx]) { order[n_order++]=active_idx; queued[active_idx]=true; }
    for (int i=0;i<c->wifi_count && i<PC_WIFI_MAX;i++) {
        if (!queued[i] && c->wifis[i].ssid[0]) { order[n_order++]=i; queued[i]=true; }
    }
    for (int k=0;k<n_order && remaining(deadline)>0;k++) {
        int idx=order[k];
        int64_t now=esp_timer_get_time();
        int64_t try_deadline=now+12000000;
        if (try_deadline>deadline || k==n_order-1) try_deadline=deadline;
        if (connect_single(c->wifis[idx].ssid,c->wifis[idx].password,try_deadline)==ESP_OK) {
            pc_config_wifi_activate(c,c->wifis[idx].ssid);
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}
void pc_wifi_off(void)
{
    if (started) { esp_wifi_stop(); started=false; }
    xEventGroupClearBits(events,1);
}
bool pc_sync_time(int64_t deadline)
{
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    if (esp_netif_sntp_init(&cfg)!=ESP_OK) return false;
    int wait=remaining(deadline);
    bool ok=wait && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(wait))==ESP_OK;
    esp_netif_sntp_deinit(); return ok;
}
/* Called by configuration mode before enabling APSTA. */
esp_err_t pc_network_ap_start(void)
{
    wifi_config_t ap={0};
    strcpy((char *)ap.ap.ssid,"PaperColor-Setup");
    ap.ap.ssid_len=strlen("PaperColor-Setup"); ap.ap.channel=1; ap.ap.max_connection=2; ap.ap.authmode=WIFI_AUTH_OPEN;
    if (started) { esp_wifi_stop(); started=false; }
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA); if (err!=ESP_OK) return err;
    err=esp_wifi_set_config(WIFI_IF_AP,&ap); if (err!=ESP_OK) return err;
    err=esp_wifi_start(); started=err==ESP_OK; return err;
}
esp_err_t pc_http_get(const pc_config_t *c,const char *path,int64_t deadline,char **body)
{
    *body=NULL;
    if (!pc_host_valid(c->host) || !remaining(deadline)) return ESP_ERR_INVALID_ARG;
    char url[640];
    if (snprintf(url,sizeof(url),"https://%s%s",c->host,path)>=(int)sizeof(url)) return ESP_ERR_INVALID_SIZE;
    esp_http_client_config_t cfg={.url=url,.timeout_ms=remaining(deadline),.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,.buffer_size=2048};
    esp_http_client_handle_t client=esp_http_client_init(&cfg); if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client,"X-QW-Api-Key",c->api_key);
    esp_http_client_set_header(client,"Accept-Encoding","gzip");
    esp_err_t err=esp_http_client_open(client,0);
    uint8_t *buffer=NULL;
    if (err!=ESP_OK) goto done;
    esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client)!=200) { err=ESP_ERR_INVALID_RESPONSE; goto done; }
    buffer=malloc(60001); if (!buffer) { err=ESP_ERR_NO_MEM; goto done; }
    size_t used=0;
    while (used<60000 && remaining(deadline)) {
        esp_http_client_set_timeout_ms(client,remaining(deadline));
        int n=esp_http_client_read(client,(char *)buffer+used,60000-used);
        if (n<0) { err=ESP_FAIL; goto done; }
        if (!n) break;
        used+=n;
    }
    if (!remaining(deadline) || !esp_http_client_is_complete_data_received(client) || used>=60000) { err=ESP_ERR_TIMEOUT; goto done; }
    buffer[used]=0;
    if (used>=2 && buffer[0]==0x1f && buffer[1]==0x8b) {
        *body=pc_gzip_decode(buffer,used,60000);
        if (!*body) err=ESP_ERR_INVALID_RESPONSE;
    }
    else { *body=(char *)buffer; buffer=NULL; }
done:
    free(buffer); esp_http_client_close(client); esp_http_client_cleanup(client); return err;
}
esp_err_t pc_city_resolve(pc_config_t *c,int64_t deadline)
{
    char encoded[300],path[420]; size_t n=0;
    static const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char *)c->city;*p;p++) {
        if (n+4>=sizeof(encoded)) return ESP_ERR_INVALID_SIZE;
        encoded[n++]='%'; encoded[n++]=hex[*p>>4]; encoded[n++]=hex[*p&15];
    }
    encoded[n]=0;
    snprintf(path,sizeof(path),"/geo/v2/city/lookup?location=%s&range=cn&number=10&lang=zh",encoded);
    char *body=NULL; esp_err_t err=pc_http_get(c,path,deadline,&body);
    pc_config_t resolved=*c;
    if (err==ESP_OK && !pc_city_parse(body,&resolved)) err=ESP_ERR_NOT_FOUND;
    if (err==ESP_OK) *c=resolved;
    free(body); return err;
}
esp_err_t pc_weather_fetch(const pc_config_t *c,pc_weather_t *weather,int64_t deadline)
{
    char path[200],*now=NULL,*daily=NULL;
    snprintf(path,sizeof(path),"/weather/v1/current/%.5f/%.5f?lang=zh&localTime=true",c->latitude,c->longitude);
    esp_err_t err=pc_http_get(c,path,deadline,&now);
    if (err==ESP_OK) {
        snprintf(path,sizeof(path),"/weather/v1/daily/%.5f/%.5f?days=3&lang=zh&localTime=true",c->latitude,c->longitude);
        err=pc_http_get(c,path,deadline,&daily);
    }
    if (err==ESP_OK && !pc_weather_parse(now,daily,c->location,time(NULL),weather)) err=ESP_ERR_INVALID_RESPONSE;
    free(now); free(daily); return err;
}
