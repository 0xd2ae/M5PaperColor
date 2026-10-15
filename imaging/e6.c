#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>

static bool ready;
void e6_init(void)
{
    if (!ready) { color_init(); ready=true; }
}
static params_t params_from(const float *v)
{
    params_t p={v[E6_EXPOSURE],v[E6_BRIGHTNESS],v[E6_CONTRAST],v[E6_SATURATION],v[E6_VIBRANCE],v[E6_TEMPERATURE],
        v[E6_TINT],v[E6_GAMMA],v[E6_CLARITY],v[E6_SHARPEN],v[E6_SMOOTHNESS],v[E6_CHROMA],(int)v[E6_MODE],(int)v[E6_REFINE]};
    if (p.chroma<=0) p.chroma=0.44f;
    if (p.refine<0) p.refine=0;
    if (p.refine>4) p.refine=4;
    return p;
}
static float *to_rgb(const uint8_t *rgba,int count)
{
    float *rgb=malloc(sizeof(float)*3*count);
    if (rgb) for (int i=0;i<count;i++) for (int k=0;k<3;k++) rgb[i*3+k]=rgba[i*4+k]/255.0f;
    return rgb;
}
/* Sobel magnitude of source lightness, roughly in L* units per pixel step. */
static void edges(const lab_t *s,int w,int h,float *edge)
{
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        #define LL(dx,dy) s[(y+(dy)<0?0:y+(dy)>=h?h-1:y+(dy))*w+(x+(dx)<0?0:x+(dx)>=w?w-1:x+(dx))].L
        float gx=LL(1,-1)+2*LL(1,0)+LL(1,1)-LL(-1,-1)-2*LL(-1,0)-LL(-1,1);
        float gy=LL(-1,1)+2*LL(0,1)+LL(1,1)-LL(-1,-1)-2*LL(0,-1)-LL(1,-1);
        #undef LL
        edge[y*w+x]=sqrtf(gx*gx+gy*gy)/4;
    }
}
int e6_process(const uint8_t *rgba,int w,int h,const float *values,uint8_t *out,int ow,int oh)
{
    e6_init();
    if (w<1 || h<1 || ow<1 || oh<1) return -1;
    params_t p=params_from(values);
    int n=ow*oh,status=-1;
    float *rgb=to_rgb(rgba,w*h),*small=malloc(sizeof(float)*3*n),*edge=malloc(sizeof(float)*n);
    lab_t *source=malloc(sizeof(lab_t)*n),*target=malloc(sizeof(lab_t)*n);
    uint8_t *region=malloc(n);
    if (rgb && small && edge && source && target && region) {
        adjust_rgb(rgb,w*h,&p);
        enhance_rgb(rgb,w,h,p.clarity);
        resample(rgb,w,h,small,ow,oh);
        unsharp_rgb(small,ow,oh,p.sharpen);
        for (int i=0;i<n;i++) source[i]=rgb_to_lab(small[i*3],small[i*3+1],small[i*3+2]);
        edges(source,ow,oh,edge);
        classify(source,edge,n,region);
        for (int i=0;i<n;i++) target[i]=map_to_panel(source[i],p.chroma);
        bool e6=p.mode!=E6_MODE_CLASSIC;
        diffuse(target,edge,region,ow,oh,p.smoothness,e6?pick_e6:pick_classic,out);
        refine(target,region,edge,ow,oh,p.refine,e6,out);
        status=0;
    }
    free(rgb); free(small); free(edge); free(source); free(target); free(region);
    return status;
}
void e6_adjust(uint8_t *rgba,int w,int h,const float *values)
{
    e6_init();
    params_t p=params_from(values);
    float *rgb=to_rgb(rgba,w*h);
    if (!rgb) return;
    adjust_rgb(rgb,w*h,&p);
    for (int i=0;i<w*h;i++) for (int k=0;k<3;k++) rgba[i*4+k]=(uint8_t)(rgb[i*3+k]*255+0.5f);
    free(rgb);
}
float e6_score(const uint8_t *rgba,int w,int h,const uint8_t *idx,int ow,int oh)
{
    e6_init();
    int n=ow*oh;
    float *rgb=to_rgb(rgba,w*h),*small=malloc(sizeof(float)*3*n);
    float score=1e9f;
    if (rgb && small) {
        resample(rgb,w,h,small,ow,oh);
        double error=0,tm=0,tv=0,rm=0,rv=0; int light=0,dirty=0;
        for (int y=0;y<oh;y++) for (int x=0;x<ow;x++) {
            int i=y*ow+x;
            lab_t s=rgb_to_lab(small[i*3],small[i*3+1],small[i*3+2]);
            lab_t t=map_to_panel(s,0.6f);
            /* 3x3 optical average of the inks around the pixel. */
            float L=0,A=0,B=0; int k=0;
            for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                int xx=x+dx,yy=y+dy; if (xx<0||yy<0||xx>=ow||yy>=oh) continue;
                const lab_t *pl=&PALETTE[idx[yy*ow+xx]]; L+=pl->L; A+=pl->a; B+=pl->b; k++;
            }
            L/=k; A/=k; B/=k;
            error+=fabsf(L-t.L)+0.6f*sqrtf((A-t.a)*(A-t.a)+(B-t.b)*(B-t.b));
            tm+=t.L; tv+=t.L*t.L; rm+=L; rv+=L*L;
            if (s.L>78 && sqrtf(s.a*s.a+s.b*s.b)<16) {
                light++;
                int ink=idx[i]; dirty+=ink==INK_BLACK||ink==INK_RED||ink==INK_BLUE;
            }
        }
        tm/=n; rm/=n;
        double ts=sqrt(fmax(0,tv/n-tm*tm)),rs=sqrt(fmax(0,rv/n-rm*rm));
        double flattening=ts>0.5?fmax(0,1-rs/ts):0;
        score=(float)(error/n+20.0*(light?(double)dirty/light:0)+8*flattening);
    }
    free(rgb); free(small);
    return score;
}
void e6_pack(const uint8_t *idx,int count,uint8_t *packed)
{
    for (int i=0;i+1<count;i+=2) packed[i/2]=(uint8_t)((idx[i]<<4)|(idx[i+1]&15));
}
