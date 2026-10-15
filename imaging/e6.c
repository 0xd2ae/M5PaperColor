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
/* Sobel magnitude of source lightness + chroma edges, roughly in L* units per pixel step. */
static void edges(const lab_t *s,int w,int h,float *edge)
{
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        #define PX(dx,dy) s[(y+(dy)<0?0:y+(dy)>=h?h-1:y+(dy))*w+(x+(dx)<0?0:x+(dx)>=w?w-1:x+(dx))]
        float gxL=PX(1,-1).L+2*PX(1,0).L+PX(1,1).L-PX(-1,-1).L-2*PX(-1,0).L-PX(-1,1).L;
        float gyL=PX(-1,1).L+2*PX(0,1).L+PX(1,1).L-PX(-1,-1).L-2*PX(0,-1).L-PX(1,-1).L;
        float gxa=PX(1,-1).a+2*PX(1,0).a+PX(1,1).a-PX(-1,-1).a-2*PX(-1,0).a-PX(-1,1).a;
        float gya=PX(-1,1).a+2*PX(0,1).a+PX(1,1).a-PX(-1,-1).a-2*PX(0,-1).a-PX(1,-1).a;
        float gxb=PX(1,-1).b+2*PX(1,0).b+PX(1,1).b-PX(-1,-1).b-2*PX(-1,0).b-PX(-1,1).b;
        float gyb=PX(-1,1).b+2*PX(0,1).b+PX(1,1).b-PX(-1,-1).b-2*PX(0,-1).b-PX(1,-1).b;
        #undef PX
        float gL = sqrtf(gxL*gxL+gyL*gyL)/4.0f;
        float gC = sqrtf(gxa*gxa+gya*gya+gxb*gxb+gyb*gyb)/8.0f;
        edge[y*w+x]=gL+0.35f*gC;
    }
}
int e6_process(const uint8_t *rgba,int w,int h,const float *values,uint8_t *out,int ow,int oh)
{
    e6_init();
    if (w<1 || h<1 || ow<1 || oh<1) return -1;
    params_t p=params_from(values);
    int n=ow*oh,status=-1;
    float *rgb=to_rgb(rgba,w*h),*small=malloc(sizeof(float)*3*n),*edge=malloc(sizeof(float)*n);
    lab_t *source=malloc(sizeof(lab_t)*n),*target_lab=malloc(sizeof(lab_t)*n);
    lin_t *target_lin=malloc(sizeof(lin_t)*n);
    uint8_t *region=malloc(n),*mask=malloc(n);
    if (rgb && small && edge && source && target_lab && target_lin && region && mask) {
        adjust_rgb(rgb,w*h,&p);
        enhance_rgb(rgb,w,h,p.clarity);
        resample(rgb,w,h,small,ow,oh);
        unsharp_rgb(small,ow,oh,p.sharpen);
        for (int i=0;i<n;i++) source[i]=rgb_to_lab(small[i*3],small[i*3+1],small[i*3+2]);
        edges(source,ow,oh,edge);
        bool e6=p.mode!=E6_MODE_CLASSIC;
        for (int i=0;i<n;i++) {
            decompose_to_panel(source[i],p.chroma,&target_lin[i],&target_lab[i],&mask[i]);
            if (!e6) mask[i]=0x3Fu;
        }
        classify(source,edge,ow,oh,region,e6?mask:NULL);
        /* When classify locked a pixel to a single pure ink (e.g. paper white or black ink),
         * snap its optical target to that ink as well so refinement agrees. */
        if (e6) {
            for (int i=0;i<n;i++) {
                if (mask[i]==(1u<<INK_WHITE)) { target_lin[i]=PALETTE_LIN[INK_WHITE]; target_lab[i]=PALETTE[INK_WHITE]; }
                else if (mask[i]==(1u<<INK_BLACK)) { target_lin[i]=PALETTE_LIN[INK_BLACK]; target_lab[i]=PALETTE[INK_BLACK]; }
            }
        }
        diffuse(target_lin,edge,region,e6?mask:NULL,ow,oh,p.smoothness,e6?pick_e6:pick_classic,out);
        refine(target_lin,target_lab,region,e6?mask:NULL,edge,ow,oh,p.refine,e6,out);
        status=0;
    }
    free(rgb); free(small); free(edge); free(source); free(target_lab); free(target_lin); free(region); free(mask);
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
        static const float K3[3][3] = {
            { 0.075f, 0.124f, 0.075f },
            { 0.124f, 0.204f, 0.124f },
            { 0.075f, 0.124f, 0.075f }
        };
        double error=0,tm=0,tv=0,rm=0,rv=0; int light=0,dirty=0;
        for (int y=0;y<oh;y++) for (int x=0;x<ow;x++) {
            int i=y*ow+x;
            lab_t s=rgb_to_lab(small[i*3],small[i*3+1],small[i*3+2]);
            lin_t t_lin; lab_t t; uint8_t m;
            decompose_to_panel(s,0.52f,&t_lin,&t,&m);
            t=lin_to_lab(t_lin);
            /* 3x3 Gaussian HVS optical average in Yule-Nielsen linear reflectance space. */
            float r_acc=0,g_acc=0,b_acc=0,w_sum=0;
            for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                int xx=x+dx,yy=y+dy; if (xx<0||yy<0||xx>=ow||yy>=oh) continue;
                float kw=K3[dy+1][dx+1];
                const lin_t *pl=&PALETTE_LIN[idx[yy*ow+xx]];
                r_acc+=kw*pl->r; g_acc+=kw*pl->g; b_acc+=kw*pl->b; w_sum+=kw;
            }
            lab_t avg=lin_to_lab((lin_t){r_acc/w_sum,g_acc/w_sum,b_acc/w_sum});
            error+=fabsf(avg.L-t.L)+0.65f*sqrtf((avg.a-t.a)*(avg.a-t.a)+(avg.b-t.b)*(avg.b-t.b));
            tm+=t.L; tv+=t.L*t.L; rm+=avg.L; rv+=avg.L*avg.L;
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
