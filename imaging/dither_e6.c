#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>

/* Visibility penalties (CIEDE2000 units) per region and ink: black, white, red, yellow, blue, green.
 * Dark/red/blue dots on light paper and cool dots in skin read as dirt far more than their Lab error suggests. */
static const float BIAS[REGIONS][INKS]={
    [REG_FLAT]={0},
    [REG_EDGE]={0},
    [REG_DARK]={0,3,4,5,4,4},
    [REG_LIGHT]={9,0,5,1,5,3},
    [REG_SKIN]={2,0,0,0,6,6},
    [REG_SKY]={1,0,6,4,0,3},
    [REG_FOLIAGE]={1,0,7,0,2,0},
};
float region_bias(uint8_t region,int ink) { return BIAS[region][ink]; }

/* Lightweight local rules, not segmentation: thresholds are on the unmapped source Lab. */
void classify(const lab_t *s,const float *edge,int count,uint8_t *region)
{
    for (int i=0;i<count;i++) {
        float c=sqrtf(s[i].a*s[i].a+s[i].b*s[i].b),h=atan2f(s[i].b,s[i].a)*57.29578f;
        if (h<0) h+=360;
        uint8_t r=REG_FLAT;
        if (edge[i]>40) r=REG_EDGE;
        else if (s[i].L<22 && c<14) r=REG_DARK;
        else if (s[i].L>78 && c<16) r=REG_LIGHT;
        else if (c>=10 && c<=55 && h>=15 && h<=75 && s[i].L>=35 && s[i].L<=88) r=REG_SKIN;
        else if (c>=8 && h>=200 && h<=280) r=REG_SKY;
        else if (c>=10 && h>=95 && h<=170) r=REG_FOLIAGE;
        region[i]=r;
    }
}
static float spread_for(uint8_t region,int ink)
{
    if (region==REG_LIGHT && ink==INK_WHITE) return 0;
    return region==REG_FLAT?1.0f:region==REG_EDGE?0.3f:0.7f;
}
int pick_e6(lab_t want,int x,int y,int w,const uint8_t *out,uint8_t region,float *chosen_de)
{
    float de[INKS]; int first=0,second=1;
    for (int ink=0;ink<INKS;ink++) de[ink]=de2000(want,PALETTE[ink]);
    if (de[second]<de[first]) { first=1; second=0; }
    for (int ink=2;ink<INKS;ink++) {
        if (de[ink]<de[first]) { second=first; first=ink; }
        else if (de[ink]<de[second]) second=ink;
    }
    int best=first; float best_score=1e9f;
    for (int ink=0;ink<INKS;ink++) {
        /* Bias only settles close calls; a clear winner keeps its colour. */
        float bias=ink==first && de[second]-de[first]>10 ? 0 : BIAS[region][ink];
        float score=de[ink]+same_neighbours(out,x,y,w,ink)*spread_for(region,ink)*fminf(1,de[ink]/6)+bias+tiebreak(x,y,ink);
        if (score<best_score) { best_score=score; best=ink; }
    }
    *chosen_de=de[best]; return best;
}
static int cluster(const uint8_t *out,int x,int y,int w,int h,int ink)
{
    int n=0;
    if (x>0 && out[y*w+x-1]==ink) n++;
    if (x+1<w && out[y*w+x+1]==ink) n++;
    if (y>0 && out[(y-1)*w+x]==ink) n++;
    if (y+1<h && out[(y+1)*w+x]==ink) n++;
    return n;
}
static float cluster_weight(uint8_t region,int ink)
{
    if (region==REG_LIGHT && ink==INK_WHITE) return 0;
    return region==REG_FLAT?0.3f:region==REG_EDGE?0.03f:0.15f;
}
/* Non-causal pass: swap an ink when the 5x5 optical average gets closer to the target average,
 * without letting any single pixel drift far from its own target. */
void refine(const lab_t *target,const uint8_t *region,const float *edge,int w,int h,int passes,bool e6,uint8_t *out)
{
    (void)edge;
    const int R=2;
    double *sum=calloc((size_t)(w+1)*(h+1)*3,sizeof(double));
    if (!sum) return;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        const lab_t *t=&target[y*w+x];
        double *s=sum+((y+1)*(w+1)+x+1)*3,*up=sum+(y*(w+1)+x+1)*3,*left=sum+((y+1)*(w+1)+x)*3,*diag=sum+(y*(w+1)+x)*3;
        s[0]=t->L+up[0]+left[0]-diag[0]; s[1]=t->a+up[1]+left[1]-diag[1]; s[2]=t->b+up[2]+left[2]-diag[2];
    }
    for (int pass=0;pass<passes;pass++) for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        int x0=x-R<0?0:x-R,x1=x+R>=w?w-1:x+R,y0=y-R<0?0:y-R,y1=y+R>=h?h-1:y+R;
        int n=(x1-x0+1)*(y1-y0+1),i=y*w+x;
        double *a=sum+(y0*(w+1)+x0)*3,*b=sum+(y0*(w+1)+x1+1)*3,*c=sum+((y1+1)*(w+1)+x0)*3,*d=sum+((y1+1)*(w+1)+x1+1)*3;
        float tL=(float)((d[0]-b[0]-c[0]+a[0])/n),ta=(float)((d[1]-b[1]-c[1]+a[1])/n),tb=(float)((d[2]-b[2]-c[2]+a[2])/n);
        float L=0,A=0,B=0;
        for (int yy=y0;yy<=y1;yy++) for (int xx=x0;xx<=x1;xx++) { const lab_t *p=&PALETTE[out[yy*w+xx]]; L+=p->L; A+=p->a; B+=p->b; }
        int old=out[i],best=old;
        float d_old=de2000(target[i],PALETTE[old]),best_cost=0;
        for (int k=-1;k<INKS;k++) {
            int ink=k<0?old:k;
            if (k>=0 && (ink==old || de2000(target[i],PALETTE[ink])>d_old+12)) continue;
            float mL=(L-PALETTE[old].L+PALETTE[ink].L)/n-tL,ma=(A-PALETTE[old].a+PALETTE[ink].a)/n-ta,mb=(B-PALETTE[old].b+PALETTE[ink].b)/n-tb;
            float cost=1.20f*mL*mL+0.58f*(ma*ma+mb*mb)+cluster(out,x,y,w,h,ink)*cluster_weight(region[i],ink);
            if (e6) cost+=region_bias(region[i],ink)*0.12f;
            /* Hysteresis: only swap for a real improvement. */
            if (k<0) best_cost=cost-0.05f;
            else if (cost<best_cost) { best_cost=cost; best=ink; }
        }
        out[i]=(uint8_t)best;
    }
    free(sum);
}
