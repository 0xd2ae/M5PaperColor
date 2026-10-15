#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clamp01(float v) { return v<0?0:v>1?1:v; }
void adjust_rgb(float *rgb,int count,const params_t *p)
{
    float gain=exp2f(p->exposure),inv_gamma=1/(p->gamma>0.1f?p->gamma:0.1f);
    float gamma_lut[1025];
    for (int i=0;i<=1024;i++) gamma_lut[i]=powf(i/1024.0f,inv_gamma);
    for (int i=0;i<count;i++) {
        float *c=rgb+i*3;
        for (int k=0;k<3;k++) {
            if (gain!=1) c[k]=linear_to_srgb(srgb_to_linear(c[k])*gain);
            c[k]=(c[k]-0.5f)*p->contrast+0.5f+p->brightness;
        }
        c[0]+=0.08f*p->temperature+0.03f*p->tint; c[2]+=-0.08f*p->temperature+0.03f*p->tint; c[1]-=0.06f*p->tint;
        float y=0.2126f*c[0]+0.7152f*c[1]+0.0722f*c[2];
        float hi=fmaxf(c[0],fmaxf(c[1],c[2])),lo=fminf(c[0],fminf(c[1],c[2]));
        /* Vibrance boosts muted colours more than saturated ones. */
        float s=clamp01(hi-lo),factor=p->saturation*(1+p->vibrance*(1-s));
        for (int k=0;k<3;k++) c[k]=gamma_lut[(int)(clamp01(y+(c[k]-y)*factor)*1024+0.5f)];
    }
}
/* Running box blur of one plane, radius r, edges clamped. */
static void box(const float *src,float *dst,int w,int h,int r)
{
    float *tmp=malloc(sizeof(float)*w*h);
    if (!tmp) { memcpy(dst,src,sizeof(float)*w*h); return; }
    for (int y=0;y<h;y++) {
        float sum=0;
        for (int k=-r;k<=r;k++) sum+=src[y*w+(k<0?0:k>=w?w-1:k)];
        for (int x=0;x<w;x++) {
            tmp[y*w+x]=sum/(2*r+1);
            int out=x-r,in=x+r+1;
            sum+=src[y*w+(in>=w?w-1:in)]-src[y*w+(out<0?0:out)];
        }
    }
    for (int x=0;x<w;x++) {
        float sum=0;
        for (int k=-r;k<=r;k++) sum+=tmp[(k<0?0:k>=h?h-1:k)*w+x];
        for (int y=0;y<h;y++) {
            dst[y*w+x]=sum/(2*r+1);
            int out=y-r,in=y+r+1;
            sum+=tmp[(in>=h?h-1:in)*w+x]-tmp[(out<0?0:out)*w+x];
        }
    }
    free(tmp);
}
/* Local contrast ("clarity") on luma plus a gentle S-curve, so regions separate before quantisation. */
void enhance_rgb(float *rgb,int w,int h,float clarity)
{
    if (clarity<=0) return;
    int n=w*h;
    float *luma=malloc(sizeof(float)*n),*base=malloc(sizeof(float)*n);
    if (luma && base) {
        for (int i=0;i<n;i++) luma[i]=0.2126f*rgb[i*3]+0.7152f*rgb[i*3+1]+0.0722f*rgb[i*3+2];
        box(luma,base,w,h,w/60+2);
        for (int i=0;i<n;i++) {
            float y=luma[i],d=(y-base[i])*1.5f*clarity;
            float curve=0.12f*clarity*(y-0.5f)*(1-fabsf(2*y-1));
            for (int k=0;k<3;k++) rgb[i*3+k]=clamp01(rgb[i*3+k]+d+curve);
        }
    }
    free(luma); free(base);
}
void unsharp_rgb(float *rgb,int w,int h,float amount)
{
    if (amount<=0) return;
    int n=w*h;
    float *plane=malloc(sizeof(float)*n),*blur=malloc(sizeof(float)*n);
    if (plane && blur) for (int k=0;k<3;k++) {
        for (int i=0;i<n;i++) plane[i]=rgb[i*3+k];
        box(plane,blur,w,h,1);
        for (int i=0;i<n;i++) rgb[i*3+k]=clamp01(plane[i]+amount*(plane[i]-blur[i]));
    }
    free(plane); free(blur);
}
