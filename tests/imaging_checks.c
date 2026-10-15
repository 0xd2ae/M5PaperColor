#include "e6.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OW 400
#define OH 600
#define W 600
#define H 900

static uint32_t hash(const uint8_t *p,size_t n)
{
    uint32_t h=2166136261u;
    for (size_t i=0;i<n;i++) h=(h^p[i])*16777619u;
    return h;
}
static void defaults(float *p,int mode)
{
    memset(p,0,sizeof(float)*E6_PARAM_COUNT);
    p[E6_CONTRAST]=1.05f; p[E6_SATURATION]=1.25f; p[E6_VIBRANCE]=0.25f; p[E6_GAMMA]=1;
    p[E6_CLARITY]=0.25f; p[E6_SHARPEN]=0.4f; p[E6_SMOOTHNESS]=25; p[E6_CHROMA]=0.44f;
    p[E6_MODE]=(float)mode; p[E6_REFINE]=2;
}
static void solid(uint8_t *rgba,int n,uint8_t r,uint8_t g,uint8_t b)
{
    for (int i=0;i<n;i++) { rgba[i*4]=r; rgba[i*4+1]=g; rgba[i*4+2]=b; rgba[i*4+3]=255; }
}
static int share(const uint8_t *idx,int n,int ink)
{
    int c=0; for (int i=0;i<n;i++) c+=idx[i]==ink; return c*100/n;
}
/* Synthetic photo-like test card: hue sweep across, lightness down, plus a white margin and a black bar. */
static void card(uint8_t *rgba)
{
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        uint8_t *p=rgba+(y*W+x)*4; float hue=x*6.0f/W,v=1-y/(float)H;
        int sector=(int)hue; float f=hue-sector,q=1-f;
        float rgb[6][3]={{1,f,0},{q,1,0},{0,1,f},{0,q,1},{f,0,1},{1,0,q}};
        for (int k=0;k<3;k++) p[k]=(uint8_t)(255*(0.15f+0.85f*v)*(0.35f+0.65f*rgb[sector%6][k]));
        if (y<60) p[0]=p[1]=p[2]=250;
        if (y>840) p[0]=p[1]=p[2]=10;
        p[3]=255;
    }
}
int main(void)
{
    e6_init();
    uint8_t *rgba=malloc(W*H*4),*idx=malloc(OW*OH),*again=malloc(OW*OH),*packed=malloc(OW*OH/2);
    float p[E6_PARAM_COUNT];
    assert(rgba && idx && again && packed);
    uint8_t palette[18]; e6_palette_rgb(palette); assert(palette[3]==151 && palette[17]==77);

    struct { uint8_t r,g,b; int ink,min; } solids[]={{255,255,255,1,90},{0,0,0,0,90},{200,30,30,2,60},{250,220,40,3,60},{30,70,190,4,60},{40,140,50,5,50},{32,32,32,0,90}};
    defaults(p,0);
    for (size_t s=0;s<sizeof(solids)/sizeof(solids[0]);s++) {
        solid(rgba,W*H,solids[s].r,solids[s].g,solids[s].b);
        assert(e6_process(rgba,W,H,p,idx,OW,OH)==0);
        int pct=share(idx,OW*OH,solids[s].ink);
        printf("solid %d: %d%% ink %d\n",(int)s,pct,solids[s].ink);
        assert(pct>=solids[s].min);
    }

    /* Tetrahedral gamut decomposition check:
     * Warm skin/peach tone (R=235, G=185, B=155) must mix only warm/neutral inks (W, R, Y, K)
     * with strictly 0% blue (ink 4) and 0% green (ink 5) confetti dots. */
    solid(rgba,W*H,235,185,155);
    assert(e6_process(rgba,W,H,p,idx,OW,OH)==0);
    assert(share(idx,OW*OH,4)==0 && share(idx,OW*OH,5)==0);
    /* Sky blue tone (R=110, G=175, B=235) must have 0% red (ink 2) confetti dots. */
    solid(rgba,W*H,110,175,235);
    assert(e6_process(rgba,W,H,p,idx,OW,OH)==0);
    assert(share(idx,OW*OH,2)==0);
    /* Foliage green tone (R=95, G=165, B=75) must have 0% red (ink 2) confetti dots. */
    solid(rgba,W*H,95,165,75);
    assert(e6_process(rgba,W,H,p,idx,OW,OH)==0);
    assert(share(idx,OW*OH,2)==0);
    card(rgba);
    for (int mode=0;mode<2;mode++) {
        defaults(p,mode);
        clock_t start=clock();
        assert(e6_process(rgba,W,H,p,idx,OW,OH)==0);
        double ms=(clock()-start)*1000.0/CLOCKS_PER_SEC;
        assert(e6_process(rgba,W,H,p,again,OW,OH)==0);
        assert(!memcmp(idx,again,OW*OH));
        for (int i=0;i<OW*OH;i++) assert(idx[i]<6);
        int used=0; for (int ink=0;ink<6;ink++) used+=share(idx,OW*OH,ink)>0;
        assert(used==6);
        /* White margin stays clean: dark, red or blue dots read as dirt. */
        int dirty=0; for (int i=0;i<OW*40;i++) dirty+=idx[i]==0||idx[i]==2||idx[i]==4;
        printf("mode %d: %.0f ms hash %08x dirty margin %d\n",mode,ms,hash(idx,OW*OH),dirty);
        FILE *f=fopen(mode?"tests/.build/e6-classic.raw":"tests/.build/e6-mix.raw","wb");
        assert(f && fwrite(idx,1,OW*OH,f)==OW*OH); fclose(f);
        assert(dirty<OW*40/50);
        e6_pack(idx,OW*OH,packed);
        assert((packed[0]>>4)==idx[0] && (packed[0]&15)==idx[1] && (packed[OW*OH/2-1]&15)==idx[OW*OH-1]);
    }
    /* Scoring prefers the same image processed sensibly over one crushed to black. */
    defaults(p,0);
    uint8_t *small=malloc(150*225*4); uint8_t sidx[100*150],dark[100*150];
    for (int y=0;y<225;y++) for (int x=0;x<150;x++) memcpy(small+(y*150+x)*4,rgba+((y*4)*W+x*4)*4,4);
    assert(e6_process(small,150,225,p,sidx,100,150)==0);
    p[E6_EXPOSURE]=-3;
    assert(e6_process(small,150,225,p,dark,100,150)==0);
    float good=e6_score(small,150,225,sidx,100,150),bad=e6_score(small,150,225,dark,100,150);
    printf("score normal %.2f, underexposed %.2f\n",good,bad);
    assert(good<bad);
    /* Neutral adjustment is an identity; +1 EV brightens the reference. */
    float neutral[E6_PARAM_COUNT]={0}; neutral[E6_CONTRAST]=1; neutral[E6_SATURATION]=1; neutral[E6_GAMMA]=1;
    uint8_t *ref=malloc(150*225*4); memcpy(ref,small,150*225*4);
    e6_adjust(ref,150,225,neutral);
    for (int i=0;i<150*225*4;i++) assert(abs(ref[i]-small[i])<=1);
    neutral[E6_EXPOSURE]=1; e6_adjust(ref,150,225,neutral);
    long before=0,after=0; for (int i=0;i<150*225;i++) { before+=small[i*4+1]; after+=ref[i*4+1]; }
    assert(after>before*11/10);
    free(ref); free(small); free(rgba); free(idx); free(again); free(packed);
    puts("Imaging checks passed");
}
