#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>

static float catmull_rom(float t)
{
    t=fabsf(t);
    if (t<1) return 1.5f*t*t*t-2.5f*t*t+1;
    if (t<2) return -0.5f*t*t*t+2.5f*t*t-4*t+2;
    return 0;
}
/* One axis of a separable resize; the kernel widens by the scale factor when shrinking to avoid aliasing. */
static void axis(const float *src,int count,int stride,int sn,float *dst,int dstride,int dn,int lines,int line_src,int line_dst)
{
    float scale=(float)sn/dn,support=scale>1?scale:1;
    int taps=(int)ceilf(2*support)*2+1;
    float *weights=malloc(sizeof(float)*taps); if (!weights) return;
    for (int o=0;o<dn;o++) {
        float center=(o+0.5f)*scale-0.5f;
        int first=(int)floorf(center-2*support)+1;
        float total=0;
        for (int k=0;k<taps;k++) { weights[k]=catmull_rom((first+k-center)/support); total+=weights[k]; }
        for (int line=0;line<lines;line++) for (int c=0;c<count;c++) {
            float sum=0;
            for (int k=0;k<taps;k++) {
                int i=first+k; i=i<0?0:i>=sn?sn-1:i;
                sum+=weights[k]*src[line*line_src+i*stride+c];
            }
            dst[line*line_dst+o*dstride+c]=sum/total;
        }
    }
    free(weights);
}
void resample(const float *src,int sw,int sh,float *dst,int dw,int dh)
{
    float *mid=malloc(sizeof(float)*3*dw*sh);
    if (!mid) return;
    axis(src,3,3,sw,mid,3,dw,sh,sw*3,dw*3);
    /* Columns: treat each column as a "line" with a row stride. */
    float scale=(float)sh/dh,support=scale>1?scale:1;
    int taps=(int)ceilf(2*support)*2+1;
    float *weights=malloc(sizeof(float)*taps);
    if (weights) for (int o=0;o<dh;o++) {
        float center=(o+0.5f)*scale-0.5f;
        int first=(int)floorf(center-2*support)+1;
        float total=0;
        for (int k=0;k<taps;k++) { weights[k]=catmull_rom((first+k-center)/support); total+=weights[k]; }
        for (int x=0;x<dw*3;x++) {
            float sum=0;
            for (int k=0;k<taps;k++) { int i=first+k; i=i<0?0:i>=sh?sh-1:i; sum+=weights[k]*mid[i*dw*3+x]; }
            float v=sum/total; dst[o*dw*3+x]=v<0?0:v>1?1:v;
        }
    }
    free(weights); free(mid);
}
