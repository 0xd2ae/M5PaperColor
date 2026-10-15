#include "render.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#define FONT(S) extern const uint8_t font##S[] __asm__("_binary_font" #S "_bin_start")
FONT(14); FONT(18); FONT(24); FONT(48); FONT(64); FONT(88); FONT(18m); FONT(24m);
extern const uint8_t icons20[] __asm__("_binary_icons20_bin_start");
extern const uint8_t icons30[] __asm__("_binary_icons30_bin_start");
extern const uint8_t icons80[] __asm__("_binary_icons80_bin_start");
static uint32_t le32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t le16(const uint8_t *p) { return p[0]|(p[1]<<8); }
static const uint8_t *font_for(int size)
{
    switch (size) { case 14:return font14; case 24:return font24; case 48:return font48; case 64:return font64; case 88:return font88; default:return font18; }
}
static const uint8_t *find(const uint8_t *font,uint32_t cp)
{
    int lo=0,hi=le32(font+8)-1;
    while (lo<=hi) {
        int mid=(lo+hi)/2; const uint8_t *p=font+12+mid*20; uint32_t code=le32(p);
        if (cp==code) return p;
        if (cp<code) hi=mid-1; else lo=mid+1;
    }
    return NULL;
}
static uint32_t next_utf8(const char **text)
{
    const uint8_t *p=(const uint8_t *)*text;
    uint32_t cp; int n;
    if (*p<0x80) { (*text)++; return *p; }
    if ((*p&0xe0)==0xc0) { cp=*p&31; n=2; }
    else if ((*p&0xf0)==0xe0) { cp=*p&15; n=3; }
    else if ((*p&0xf8)==0xf0) { cp=*p&7; n=4; }
    else { (*text)++; return '?'; }
    for (int i=1;i<n;i++) { if (!p[i] || (p[i]&0xc0)!=0x80) { (*text)++; return '?'; } cp=(cp<<6)|(p[i]&63); }
    *text+=n; return cp;
}
static void pixel(uint8_t *p,int x,int y,pc_color_t color)
{
    if (x>=0 && x<PC_WIDTH && y>=0 && y<PC_HEIGHT) p[y*PC_WIDTH+x]=color;
}
static void rect(uint8_t *p,int x,int y,int w,int h,pc_color_t color)
{
    int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>PC_WIDTH?PC_WIDTH:x+w,y1=y+h>PC_HEIGHT?PC_HEIGHT:y+h;
    for (int row=y0;row<y1;row++) if (x1>x0) memset(p+row*PC_WIDTH+x0,color,x1-x0);
}
static void outline_rect(uint8_t *p,int x,int y,int w,int h,pc_color_t color,int width)
{
    rect(p,x,y,w,width,color);
    rect(p,x,y+h-width,w,width,color);
    rect(p,x,y,width,h,color);
    rect(p,x+w-width,y,width,h,color);
}
static const uint8_t *medium_font_for(int size)
{
    switch (size) { case 24:return font24m; case 18:return font18m; default:return font_for(size); }
}
static int font_width(const uint8_t *font,const char *str)
{
    int width=0;
    while (*str) { const uint8_t *g=find(font,next_utf8(&str)); if (!g) g=find(font,'?'); if (g) width+=g[12]; }
    return width;
}
static int text_width(const char *str,int size)
{
    return font_width(font_for(size),str);
}
static void font_text(uint8_t *pixels,const uint8_t *font,int x,int y,const char *str,pc_color_t color,int max_width)
{
    int start=x;
    const uint8_t *data=font+12+le32(font+8)*20;
    while (*str) {
        const uint8_t *g=find(font,next_utf8(&str)); if (!g) g=find(font,'?');
        if (!g) continue;
        int advance=g[12],w=g[10],h=g[11];
        if (x+advance>start+max_width) break;
        int gx=x+(int8_t)g[13],gy=y+(int8_t)g[14],offset=0;
        const uint8_t *bitmap=data+le32(g+4);
        for (int i=0;i<le16(g+8);i++) {
            int run=bitmap[i]&127;
            if (bitmap[i]&128) for (int j=0;j<run && offset+j<w*h;j++) pixel(pixels,gx+(offset+j)%w,gy+(offset+j)/w,color);
            offset+=run;
        }
        x+=advance;
    }
}
static void text(uint8_t *pixels,int x,int y,const char *str,int size,pc_color_t color,int max_width)
{
    font_text(pixels,font_for(size),x,y,str,color,max_width);
}
static void centered(uint8_t *p,int cx,int y,const char *str,int size,pc_color_t color,int width)
{
    int w=text_width(str,size); if (w>width) w=width;
    text(p,cx-w/2,y,str,size,color,width);
}
static void medium_text(uint8_t *p,int x,int y,const char *str,int size,pc_color_t color,int width)
{
    font_text(p,medium_font_for(size),x,y,str,color,width);
}
static void bold_text(uint8_t *p,int x,int y,const char *str,int size,pc_color_t color,int width)
{
    text(p,x,y,str,size,color,width);
    text(p,x+1,y,str,size,color,width);
    if (size<=14) return;
    text(p,x,y+1,str,size,color,width);
    text(p,x+1,y+1,str,size,color,width);
}
static void bold_centered(uint8_t *p,int cx,int y,const char *str,int size,pc_color_t color,int width)
{
    int w=text_width(str,size); if (w>width) w=width;
    bold_text(p,cx-w/2,y,str,size,color,width);
}
static const uint8_t *icon_find(const uint8_t *pack,unsigned key)
{
    int low=0,high=le16(pack+6)-1;
    while (low<=high) {
        int mid=(low+high)/2;
        const uint8_t *entry=pack+8+mid*12;
        unsigned code=le16(entry);
        if (code==key) return entry;
        if (key<code) high=mid-1; else low=mid+1;
    }
    return NULL;
}
static void icon_mask(uint8_t *pixels,int x,int y,const uint8_t *pack,const uint8_t *entry,pc_color_t color)
{
    if (!entry) return;
    int size=le16(pack+4),at=0;
    const uint8_t *data=pack+8+le16(pack+6)*12+le32(entry+4);
    for (uint32_t i=0;i<le32(entry+8);i++) {
        int run=data[i]&127;
        if (data[i]&128) for (int j=0;j<run && at+j<size*size;j++)
            pixel(pixels,x+(at+j)%size,y+(at+j)/size,color);
        at+=run;
    }
}
int pc_render_weather_icon(uint8_t *pixels,int x,int y,int code,int size)
{
    if (size!=20 && size!=30 && size!=80) return 0;
    const uint8_t *pack=size==20?icons20:size==30?icons30:icons80;
    if (code<100 || code>999 || !icon_find(pack,(unsigned)code*2)) code=999;
    pc_color_t fill=code==100?PC_YELLOW:code==900?PC_RED:code==901?PC_BLUE:
        code>=500 && code<600?PC_YELLOW:PC_BLUE;
    const uint8_t *solid=icon_find(pack,(unsigned)code*2+1);
    icon_mask(pixels,x,y,pack,solid,fill);
    icon_mask(pixels,x,y,pack,icon_find(pack,(unsigned)code*2),solid?PC_BLACK:fill);
    return code;
}
void pc_render_test(uint8_t *p)
{
    memset(p,PC_WHITE,PC_PIXELS);
    const char *names[]={"黑","白","红","黄","蓝","绿"};
    for (int i=0;i<6;i++) { rect(p,20,30+i*75,360,55,i); text(p,30,35+i*75,names[i],24,i==PC_BLACK?PC_WHITE:PC_BLACK,300); }
    text(p,20,500,"中文测试 霞鹜文楷",24,PC_BLACK,360);
    text(p,20,540,"A 上月  B 下月  C 今天",18,PC_BLACK,360);
}
static void render_header(uint8_t *p,const pc_screen_t *s,const pc_holidays_t *holidays)
{
    char buf[16];
    snprintf(buf,sizeof(buf),s->battery_percent<0?"电量 --":"电量 %d%%",s->battery_percent);
    int right=384-text_width(buf,14)-1;
    bold_text(p,right,9,buf,14,PC_BLACK,384-right);
    const char *status="";
    if (s->time_valid && !pc_holidays_has_year(holidays,s->view_year)) status="调休数据未导入";
    else if (s->time_valid && s->weather.valid && time(NULL)-s->weather.fetched>10800) status="天气已过期";
    else if (s->time_valid && s->offline) status="离线";
    if (*status) {
        right-=text_width(status,14)+13;
        bold_text(p,right,9,status,14,PC_RED,text_width(status,14)+1);
    }
    int limit=right-28;
    const char *city=s->city[0]?s->city:"天气日历";
    if (text_width(city,18)>limit) {
        medium_text(p,16,6,city,18,PC_BLACK,limit-18); text(p,16+limit-18,6,"…",18,PC_BLACK,18);
    } else medium_text(p,16,6,city,18,PC_BLACK,limit);
    rect(p,16,32,368,2,PC_BLACK);
}
static void render_indoor(uint8_t *p,int x,int y,const pc_screen_t *s,int width)
{
    static const char *formats[]={"室内 %d℃ %d%%","室内%d℃ %d%%","%d℃ %d%%"};
    char buf[32];
    if (!s->indoor_valid) snprintf(buf,sizeof(buf),"室内 --");
    else for (int i=0;i<3;i++) {
        snprintf(buf,sizeof(buf),formats[i],s->indoor_temperature,s->indoor_humidity);
        if (text_width(buf,18)+1<=width) break;
    }
    medium_text(p,x,y,buf,18,PC_GREEN,width);
}
static void render_weather(uint8_t *p,const pc_screen_t *s)
{
    char buf[32];
    pc_render_weather_icon(p,16,38,s->weather.valid?s->weather.icon:999,80);
    if (!s->weather.valid) {
        text(p,112,44,"天气暂无数据",24,PC_BLACK,272);
        text(p,112,74,"请配置并联网",18,PC_BLUE,272);
        render_indoor(p,112,98,s,272);
        rect(p,16,124,368,1,PC_BLACK);
        return;
    }
    snprintf(buf,sizeof(buf),"%d°",s->weather.temperature);
    bold_text(p,108,40,buf,64,PC_BLACK,134);
    medium_text(p,250,40,s->weather.text,24,PC_BLUE,134);
    snprintf(buf,sizeof(buf),"%d / %d℃",s->weather.days[0].low,s->weather.days[0].high);
    text(p,250,72,buf,18,PC_BLACK,134);
    render_indoor(p,250,98,s,134);
    rect(p,16,124,368,1,PC_BLACK);
    for (int i=0;i<3;i++) {
        int x=16+i*125;
        pc_date_t forecast_date;
        const char *label=NULL;
        if (pc_parse_date(s->weather.days[i].date,&forecast_date)) {
            int delta=pc_day_number(forecast_date)-pc_day_number(s->today);
            if (delta>=0 && delta<=2) label=delta==0?"今天":delta==1?"明天":"后天";
        }
        if (label) snprintf(buf,sizeof(buf),"%s",label);
        else snprintf(buf,sizeof(buf),"%.5s",s->weather.days[i].date+5);
        bold_text(p,x,129,buf,18,PC_BLACK,72);
        pc_render_weather_icon(p,x+81,141,s->weather.days[i].icon,30);
        snprintf(buf,sizeof(buf),"%d/%d℃",s->weather.days[i].low,s->weather.days[i].high);
        text(p,x,154,buf,18,PC_BLACK,78);
        if (i<2) rect(p,x+117,132,1,48,PC_BLACK);
    }
    rect(p,16,186,368,1,PC_BLACK);
}
static void render_today(uint8_t *p,const pc_screen_t *s,const pc_holidays_t *holidays)
{
    char buf[96];
    if (!s->time_valid) {
        rect(p,16,192,76,44,PC_BLUE);
        bold_centered(p,54,200,"?",24,PC_WHITE,70);
        bold_text(p,104,190,"等待校时",24,PC_BLACK,280);
        text(p,104,218,"联网后显示日期与月历",18,PC_BLUE,280);
        return;
    }
    rect(p,16,192,76,44,PC_BLUE);
    snprintf(buf,sizeof(buf),"%d",s->today.day);
    bold_centered(p,54,185,buf,48,PC_WHITE,72);
    static const char *week[]={"星期一","星期二","星期三","星期四","星期五","星期六","星期日"};
    const char *weekday=week[pc_weekday(s->today)];
    bold_text(p,104,190,weekday,24,PC_BLUE,100);
    char lunar[48]; pc_lunar_label(s->today,true,lunar,sizeof(lunar));
    const char *lunar_day=strchr(lunar,' ');
    snprintf(buf,sizeof(buf),"农历%s",lunar_day?lunar_day+1:lunar);
    text(p,112+text_width(weekday,24),196,buf,18,PC_BLACK,272-text_width(weekday,24));
    snprintf(buf,sizeof(buf),"%d年%d月%d日",s->today.year,s->today.month,s->today.day);
    text(p,104,218,buf,18,PC_BLACK,280);
    int x=112+text_width(buf,18);
    const char *name; pc_day_kind_t kind=pc_holidays_kind(holidays,s->today,&name);
    const char *festival=pc_festival(s->today),*term=pc_solar_term(s->today);
    char fest[32];
    snprintf(fest,sizeof(fest),"%s%s%s",festival,*festival && *term?" · ":"",term);
    if (kind==PC_WORK) snprintf(buf,sizeof(buf),"调休补班%s%s",*fest?" · ":"",fest);
    else if (kind==PC_REST) snprintf(buf,sizeof(buf),"%s · 休",name);
    else snprintf(buf,sizeof(buf),"%s",fest);
    bold_text(p,x,218,buf,18,kind==PC_WORK?PC_BLACK:PC_RED,384-x);
}
static void render_month(uint8_t *p,const pc_screen_t *s,const pc_holidays_t *holidays)
{
    char buf[32];
    rect(p,16,241,368,1,PC_BLACK);
    snprintf(buf,sizeof(buf),"%d年 %d月",s->view_year,s->view_month);
    bold_centered(p,200,245,buf,24,PC_BLACK,368);
    static const char *week[]={"一","二","三","四","五","六","日"};
    for (int i=0;i<7;i++) {
        int x=18+i*52;
        bold_centered(p,x+26,276,week[i],18,i>=5?PC_RED:PC_BLACK,48);
    }
    rect(p,16,297,368,1,PC_BLACK);
    pc_date_t first={s->view_year,s->view_month,1};
    int offset=pc_weekday(first),start=pc_day_number(first)-offset;
    int rows=(offset+pc_month_days(s->view_year,s->view_month)+6)/7;
    int row_h=296/rows; if (row_h>58) row_h=58;
    for (int i=0;i<rows*7;i++) {
        pc_date_t d=pc_date_from_day(start+i);
        if (d.month!=s->view_month) continue;
        int x=18+(i%7)*52,cell=300+(i/7)*row_h,top=cell+(row_h-46)/2,cx=x+26;
        pc_day_kind_t kind=pc_holidays_kind(holidays,d,NULL);
        bool weekend=i%7>=5;
        bool today=d.year==s->today.year && d.month==s->today.month && d.day==s->today.day;
        pc_color_t day_color=kind==PC_REST?PC_RED:(weekend && kind!=PC_WORK)?PC_RED:PC_BLACK;
        pc_color_t lunar_color=PC_BLACK;
        if (today) { rect(p,x+1,cell+1,50,row_h-2,PC_BLUE); day_color=lunar_color=PC_WHITE; }
        snprintf(buf,sizeof(buf),"%d",d.day);
        bold_centered(p,cx-(kind!=PC_NORMAL?8:0),top-1,buf,24,day_color,40);
        if (kind!=PC_NORMAL) {
            if (kind==PC_REST) rect(p,x+35,top+1,17,18,PC_RED);
            else outline_rect(p,x+35,top+1,17,18,PC_BLACK,1);
            text(p,x+37,top+3,kind==PC_REST?"休":"班",14,kind==PC_REST?PC_WHITE:PC_BLACK,14);
        }
        const char *fest=pc_festival(d);
        if (!*fest) fest=pc_solar_term(d);
        if (*fest) {
            snprintf(buf,sizeof(buf),"%s",fest); if (!today) lunar_color=PC_GREEN;
            int size=text_width(buf,18)<50?18:14,y=size==18?top+22:top+25;
            /* Widen vertical strokes only: 18 px glyphs such as 重 have no room for thicker horizontals. */
            centered(p,cx,y,buf,size,lunar_color,50);
            centered(p,cx+1,y,buf,size,lunar_color,50);
            continue;
        }
        pc_lunar_label(d,false,buf,sizeof(buf));
        if (text_width(buf,18)<=50) centered(p,cx,top+22,buf,18,lunar_color,50);
        else centered(p,cx,top+25,buf,14,lunar_color,50);
    }
}
static void render_setup(uint8_t *p)
{
    rect(p,12,244,376,348,PC_WHITE);
    outline_rect(p,12,244,376,348,PC_BLUE,3);
    bold_text(p,32,268,"手机连接开放热点",24,PC_BLUE,336);
    bold_text(p,32,310,"PaperColor-Setup",24,PC_BLACK,336);
    text(p,32,356,"无需密码",18,PC_BLACK,336);
    text(p,32,396,"浏览器打开 192.168.4.1",18,PC_BLACK,336);
    bold_text(p,32,436,"顶部 C 退出配置",18,PC_BLUE,336);
    text(p,32,476,"10分钟无操作自动退出",18,PC_BLACK,336);
}
void pc_render(uint8_t *p,const pc_screen_t *s,const pc_holidays_t *holidays)
{
    memset(p,PC_WHITE,PC_PIXELS);
    render_header(p,s,holidays);
    render_weather(p,s);
    render_today(p,s,holidays);
    if (s->time_valid) render_month(p,s,holidays);
    if (s->configuring) render_setup(p);
}
