#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t BAYER[8][8]={
    {0,32,8,40,2,34,10,42},{48,16,56,24,50,18,58,26},{12,44,4,36,14,46,6,38},{60,28,52,20,62,30,54,22},
    {3,35,11,43,1,33,9,41},{51,19,59,27,49,17,57,25},{15,47,7,39,13,45,5,37},{63,31,55,23,61,29,53,21}};

/* Small per-ink rotated screen: only breaks near-ties, so inks don't share one period. */
float tiebreak(int x,int y,int ink)
{
    return BAYER[(y+ink*3)&7][(x+ink*5)&7]*(0.35f/64);
}
/* Already-quantised neighbours: previous in the serpentine scan, upper-left, up, upper-right. */
int same_neighbours(const uint8_t *out,int x,int y,int w,int ink)
{
    int n=0,prev=y&1?x+1:x-1;
    if (prev>=0 && prev<w && out[y*w+prev]==ink) n++;
    if (y>0) {
        const uint8_t *up=out+(y-1)*w;
        if (up[x]==ink) n++;
        if (x>0 && up[x-1]==ink) n++;
        if (x+1<w && up[x+1]==ink) n++;
    }
    return n;
}
int pick_classic(lab_t want,int x,int y,int w,const uint8_t *out,uint8_t region,float *chosen_de)
{
    int best=0; float best_score=1e9f,best_de=0;
    float spread=region==REG_FLAT?0.9f:region==REG_EDGE?0.25f:0.6f;
    for (int ink=0;ink<INKS;ink++) {
        float de=de2000(want,PALETTE[ink]);
        float score=de+same_neighbours(out,x,y,w,ink)*spread*fminf(1,de/6)+tiebreak(x,y,ink);
        if (score<best_score) { best_score=score; best=ink; best_de=de; }
    }
    *chosen_de=best_de; return best;
}
static float clampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
/* Stucki diffusion in panel Lab. Strong edges drop the second future row (renormalised) so colour
 * doesn't bleed across boundaries; near-exact matches stop diffusing (smoothness snap). */
void diffuse(const lab_t *target,const float *edge,const uint8_t *region,int w,int h,float smoothness,picker_t pick,uint8_t *out)
{
    lab_t *rows[3];
    for (int i=0;i<3;i++) rows[i]=calloc(w+4,sizeof(lab_t));
    if (!rows[0] || !rows[1] || !rows[2]) { for (int i=0;i<3;i++) free(rows[i]); return; }
    float threshold=smoothness*0.4f,hard=threshold*0.5f;
    for (int y=0;y<h;y++) {
        bool reverse=y&1;
        for (int step=0;step<w;step++) {
            int x=reverse?w-1-step:step,i=y*w+x,dir=reverse?-1:1;
            lab_t e=rows[0][x+2],t=target[i];
            lab_t want={t.L+clampf(e.L,-30,30),t.a+clampf(e.a,-45,45),t.b+clampf(e.b,-45,45)};
            float de;
            int ink=pick(want,x,y,w,out,region[i],&de);
            out[i]=(uint8_t)ink;
            float factor=1;
            if (threshold>0) factor=de<=hard?0:de>=threshold?1:(de-hard)/(threshold-hard);
            lab_t err={(want.L-PALETTE[ink].L)*factor,(want.a-PALETTE[ink].a)*factor,(want.b-PALETTE[ink].b)*factor};
            float far=clampf(1-(edge[i]-14)/58,0,1),norm=1/(32+10*far);
            static const float near0[2]={8,4},near1[5]={2,4,8,4,2},far2[5]={1,2,4,2,1};
            for (int k=0;k<2;k++) {
                int xx=x+dir*(k+1); if (xx<0||xx>=w) continue;
                float f=near0[k]*norm; lab_t *d=&rows[0][xx+2]; d->L+=err.L*f; d->a+=err.a*f; d->b+=err.b*f;
            }
            for (int k=0;k<5;k++) {
                int xx=x+dir*(k-2); if (xx<0||xx>=w) continue;
                float f=near1[k]*norm; lab_t *d=&rows[1][xx+2]; d->L+=err.L*f; d->a+=err.a*f; d->b+=err.b*f;
                f=far2[k]*far*norm; d=&rows[2][xx+2]; d->L+=err.L*f; d->a+=err.a*f; d->b+=err.b*f;
            }
        }
        lab_t *done=rows[0]; rows[0]=rows[1]; rows[1]=rows[2]; rows[2]=done;
        memset(rows[2],0,sizeof(lab_t)*(w+4));
    }
    for (int i=0;i<3;i++) free(rows[i]);
}
