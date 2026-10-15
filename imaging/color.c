#include "e6_internal.h"
#include <math.h>

/* Panel reflectance readings published in PaperColor-Frame doc/pipeline.md (colorimeter, 2026-05-21). */
static uint8_t palette_rgb[INKS][3]={
    {72,71,82},{151,161,161},{120,70,71},{164,154,69},{60,99,136},{79,103,77}};
lab_t PALETTE[INKS];
static float to_linear[4096];

static float srgb_curve(float c)
{
    return c<=0.04045f ? c/12.92f : powf((c+0.055f)/1.055f,2.4f);
}
float srgb_to_linear(float c)
{
    if (c<=0) return 0;
    if (c>=1) return 1;
    return to_linear[(int)(c*4095.0f+0.5f)];
}
float linear_to_srgb(float c)
{
    if (c<=0) return 0;
    if (c>=1) return 1;
    return c<=0.0031308f ? c*12.92f : 1.055f*powf(c,1/2.4f)-0.055f;
}
static float lab_f(float t)
{
    const float e=216.0f/24389.0f;
    return t>e ? cbrtf(t) : t/(3*(6.0f/29)*(6.0f/29))+4.0f/29;
}
lab_t rgb_to_lab(float r,float g,float b)
{
    r=srgb_to_linear(r); g=srgb_to_linear(g); b=srgb_to_linear(b);
    float x=(0.4124564f*r+0.3575761f*g+0.1804375f*b)/0.95047f;
    float y=0.2126729f*r+0.7151522f*g+0.0721750f*b;
    float z=(0.0193339f*r+0.1191920f*g+0.9503041f*b)/1.08883f;
    float fx=lab_f(x),fy=lab_f(y),fz=lab_f(z);
    return (lab_t){116*fy-16,500*(fx-fy),200*(fy-fz)};
}
static void update_palette(void)
{
    for (int i=0;i<INKS;i++) PALETTE[i]=rgb_to_lab(palette_rgb[i][0]/255.0f,palette_rgb[i][1]/255.0f,palette_rgb[i][2]/255.0f);
}
void color_init(void)
{
    for (int i=0;i<4096;i++) to_linear[i]=srgb_curve(i/4095.0f);
    update_palette();
}
void e6_set_palette_rgb(const uint8_t *rgb)
{
    for (int i=0;i<INKS*3;i++) palette_rgb[i/3][i%3]=rgb[i];
    update_palette();
}
void e6_palette_rgb(uint8_t *rgb)
{
    for (int i=0;i<INKS*3;i++) rgb[i]=palette_rgb[i/3][i%3];
}
float de76_sq(lab_t x,lab_t y)
{
    float l=x.L-y.L,a=x.a-y.a,b=x.b-y.b;
    return l*l+a*a+b*b;
}
static float degrees(float r) { return r*57.29577951f; }
static float radians(float d) { return d*0.01745329252f; }
static float hue(float b,float a)
{
    if (a==0 && b==0) return 0;
    float h=degrees(atan2f(b,a));
    return h<0 ? h+360 : h;
}
float de2000(lab_t x,lab_t y)
{
    const float p7=6103515625.0f; /* 25^7 */
    float c1=sqrtf(x.a*x.a+x.b*x.b),c2=sqrtf(y.a*y.a+y.b*y.b),cb=(c1+c2)/2;
    float cb7=powf(cb,7),g=0.5f*(1-sqrtf(cb7/(cb7+p7)));
    float a1=(1+g)*x.a,a2=(1+g)*y.a;
    float c1p=sqrtf(a1*a1+x.b*x.b),c2p=sqrtf(a2*a2+y.b*y.b);
    float h1=hue(x.b,a1),h2=hue(y.b,a2);
    float dl=y.L-x.L,dc=c2p-c1p,dh=0;
    if (c1p*c2p!=0) { dh=h2-h1; if (dh>180) dh-=360; else if (dh<-180) dh+=360; }
    float dH=2*sqrtf(c1p*c2p)*sinf(radians(dh/2));
    float lb=(x.L+y.L)/2,cbp=(c1p+c2p)/2,hb;
    if (c1p*c2p==0) hb=h1+h2;
    else if (fabsf(h1-h2)<=180) hb=(h1+h2)/2;
    else hb=h1+h2<360 ? (h1+h2+360)/2 : (h1+h2-360)/2;
    float t=1-0.17f*cosf(radians(hb-30))+0.24f*cosf(radians(2*hb))+0.32f*cosf(radians(3*hb+6))-0.20f*cosf(radians(4*hb-63));
    float theta=30*expf(-((hb-275)/25)*((hb-275)/25));
    float cbp7=powf(cbp,7),rc=2*sqrtf(cbp7/(cbp7+p7));
    float l50=(lb-50)*(lb-50);
    float sl=1+0.015f*l50/sqrtf(20+l50),sc=1+0.045f*cbp,sh=1+0.015f*cbp*t;
    float rt=-sinf(radians(2*theta))*rc;
    float kl=dl/sl,kc=dc/sc,kh=dH/sh;
    return sqrtf(kl*kl+kc*kc+kh*kh+rt*kc*kh);
}
/* Compress source lightness into the measured black..white range and keep a fraction of the chroma,
 * offset by the paper's own black/white tint. A small toe sends deep shadows to solid ink black:
 * the panel's black is around L*30, so without it L*10-20 shadows sit just above black and pick up
 * the darkest coloured inks as speckle. */
lab_t map_to_panel(lab_t s,float chroma)
{
    lab_t k=PALETTE[INK_BLACK],w=PALETTE[INK_WHITE];
    float t=(s.L/100-0.06f)/0.94f; t=t<0?0:t>1?1:t; t=powf(t,0.92f);
    return (lab_t){k.L+(w.L-k.L)*t, k.a+(w.a-k.a)*t+s.a*chroma, k.b+(w.b-k.b)*t+s.b*chroma};
}
