#include "e6_internal.h"
#include <math.h>

/* Panel reflectance readings published in PaperColor-Frame doc/pipeline.md (colorimeter, 2026-05-21). */
static uint8_t palette_rgb[INKS][3]={
    {72,71,82},{151,161,161},{120,70,71},{164,154,69},{60,99,136},{79,103,77}};
lab_t PALETTE[INKS];
lin_t PALETTE_LIN[INKS];
static float to_linear[4096];

/* Yule-Nielsen optical dot-gain exponent (1/n with n ≈ 1.15).
 * Reflective pigment halftones mix linearly in R^(1/n) rather than non-linear Lab. */
static const float YN_INV = 0.87f;
static const float YN_FWD = 1.0f / 0.87f;

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
lin_t srgb_to_optical(float r, float g, float b)
{
    r=srgb_to_linear(r); g=srgb_to_linear(g); b=srgb_to_linear(b);
    return (lin_t){powf(r, YN_INV), powf(g, YN_INV), powf(b, YN_INV)};
}
static float lab_f(float t)
{
    const float e=216.0f/24389.0f;
    return t>e ? cbrtf(t) : t/(3*(6.0f/29)*(6.0f/29))+4.0f/29;
}
lab_t lin_to_lab(lin_t c)
{
    float r=powf(c.r<0?0:c.r>1.5f?1.5f:c.r, YN_FWD);
    float g=powf(c.g<0?0:c.g>1.5f?1.5f:c.g, YN_FWD);
    float b=powf(c.b<0?0:c.b>1.5f?1.5f:c.b, YN_FWD);
    float x=(0.4124564f*r+0.3575761f*g+0.1804375f*b)/0.95047f;
    float y=0.2126729f*r+0.7151522f*g+0.0721750f*b;
    float z=(0.0193339f*r+0.1191920f*g+0.9503041f*b)/1.08883f;
    float fx=lab_f(x),fy=lab_f(y),fz=lab_f(z);
    return (lab_t){116*fy-16,500*(fx-fy),200*(fy-fz)};
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
static float degrees(float r) { return r*57.29577951f; }
static float radians(float d) { return d*0.01745329252f; }
static float hue(float b,float a)
{
    if (a==0 && b==0) return 0;
    float h=degrees(atan2f(b,a));
    return h<0 ? h+360 : h;
}

/* The 4 chromatic primaries sorted around the a*b* hue circle: Red -> Yellow -> Green -> Blue.
 * Together with the K-W neutral axis they partition the 6-ink gamut into 4 hue-sector tetrahedra. */
static const int HUE_ORDER[4] = { INK_RED, INK_YELLOW, INK_GREEN, INK_BLUE };
static float primary_hue[4];
static float primary_da[4], primary_db[4], primary_dL[4];

static void update_palette(void)
{
    for (int i=0;i<INKS;i++) {
        float r=palette_rgb[i][0]/255.0f, g=palette_rgb[i][1]/255.0f, b=palette_rgb[i][2]/255.0f;
        PALETTE[i]=rgb_to_lab(r,g,b);
        PALETTE_LIN[i]=srgb_to_optical(r,g,b);
    }
    lab_t k=PALETTE[INK_BLACK], w=PALETTE[INK_WHITE];
    float dL_kw = w.L - k.L;
    if (fabsf(dL_kw) < 1e-3f) dL_kw = 1.0f;
    for (int s=0;s<4;s++) {
        int ink = HUE_ORDER[s];
        lab_t p = PALETTE[ink];
        float t_p = (p.L - k.L) / dL_kw;
        float na = k.a + (w.a - k.a) * t_p;
        float nb = k.b + (w.b - k.b) * t_p;
        primary_da[s] = p.a - na;
        primary_db[s] = p.b - nb;
        primary_dL[s] = p.L;
        primary_hue[s] = hue(primary_db[s], primary_da[s]);
    }
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

/* Hue-preserving tetrahedral barycentric decomposition into the 6-ink convex hull:
 * 1. Projects the panel-mapped target chroma onto the 2D hue sector spanned by two adjacent
 *    chromatic primaries (C1, C2) around the neutral K-W axis.
 * 2. Solves for the barycentric weights (wK, wW, wC1, wC2) >= 0 with wK + wW + wC1 + wC2 = 1.
 * 3. Returns the compatible ink mask (at most 4 compatible inks per pixel, strictly 0% opposite
 *    complementary inks) and the Yule-Nielsen linear optical reflectance target. */
void decompose_to_panel(lab_t s, float chroma, lin_t *out_lin, lab_t *out_lab, uint8_t *out_mask)
{
    lab_t mapped = map_to_panel(s, chroma);
    lab_t k = PALETTE[INK_BLACK], w = PALETTE[INK_WHITE];
    float dL_kw = w.L - k.L;
    float t = (mapped.L - k.L) / dL_kw;
    t = t < 0 ? 0 : t > 1 ? 1 : t;

    /* Chroma vector relative to the panel's K-W neutral axis at lightness t.
     * Normalise against the reference chroma (0.44) so saturated primaries reach the tetrahedron's
     * chromatic vertices (u1 + u2 -> 1.0) while respecting the user's chroma slider. */
    float chroma_scale = (chroma / 0.44f) * 0.56f;
    float ca = s.a * chroma_scale;
    float cb = s.b * chroma_scale;
    float c_mag = sqrtf(ca * ca + cb * cb);

    /* Near-neutral pixels: only use Black and White. */
    if (c_mag < 1.0f) {
        float wW = t, wK = 1.0f - t;
        lin_t lk = PALETTE_LIN[INK_BLACK], lw = PALETTE_LIN[INK_WHITE];
        lin_t mix = {
            wK * lk.r + wW * lw.r,
            wK * lk.g + wW * lw.g,
            wK * lk.b + wW * lw.b
        };
        *out_lin = mix;
        *out_lab = mapped;
        uint8_t mask = 0;
        if (wK > 0.01f) mask |= (1u << INK_BLACK);
        if (wW > 0.01f) mask |= (1u << INK_WHITE);
        *out_mask = mask ? mask : (1u << (wW >= 0.5f ? INK_WHITE : INK_BLACK));
        return;
    }

    /* Find which of the 4 hue sectors (R-Y, Y-G, G-B, B-R) contains (ca, cb). */
    int sec = 0;
    float u1 = 0, u2 = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        float a1 = primary_da[i], b1 = primary_db[i];
        float a2 = primary_da[j], b2 = primary_db[j];
        float det = a1 * b2 - b1 * a2;
        if (fabsf(det) < 1e-4f) continue;
        float inv = 1.0f / det;
        float x1 = (ca * b2 - cb * a2) * inv;
        float x2 = (a1 * cb - b1 * ca) * inv;
        if (x1 >= -1e-4f && x2 >= -1e-4f) {
            sec = i;
            u1 = x1 < 0 ? 0 : x1;
            u2 = x2 < 0 ? 0 : x2;
            break;
        }
    }

    int idx1 = HUE_ORDER[sec], idx2 = HUE_ORDER[(sec + 1) & 3];
    float u_sum = u1 + u2;

    /* Clamp total chromatic weight to <= 1 when the target exceeds the panel's outer gamut boundary. */
    if (u_sum > 1.0f) {
        float scale = 1.0f / u_sum;
        u1 *= scale;
        u2 *= scale;
        u_sum = 1.0f;
    }

    /* Target lightness inside panel range [k.L, w.L]. */
    float target_L = mapped.L;
    float L_c = u1 * PALETTE[idx1].L + u2 * PALETTE[idx2].L;
    float rem = 1.0f - u_sum;
    float min_L = L_c + rem * k.L;
    float max_L = L_c + rem * w.L;

    /* Smoothly scale chroma when target_L approaches the white/black apex so there is no contour ridge. */
    if (target_L > max_L && u_sum > 1e-4f) {
        float c_norm_L = L_c / u_sum;
        float denom = w.L - c_norm_L;
        if (denom > 1e-3f) {
            float u_fit = (w.L - target_L) / denom;
            float u_new = 0.70f * u_fit + 0.30f * u_sum;
            if (u_new < 0) u_new = 0;
            if (u_new < u_sum) {
                float s_scale = u_new / u_sum;
                u1 *= s_scale; u2 *= s_scale; u_sum = u_new;
            }
        }
    } else if (target_L < min_L && u_sum > 1e-4f) {
        float c_norm_L = L_c / u_sum;
        float denom = c_norm_L - k.L;
        if (denom > 1e-3f) {
            float u_fit = (target_L - k.L) / denom;
            float u_new = 0.72f * u_fit + 0.28f * u_sum;
            if (u_new < 0) u_new = 0;
            if (u_new < u_sum) {
                float s_scale = u_new / u_sum;
                u1 *= s_scale; u2 *= s_scale; u_sum = u_new;
            }
        }
    }

    rem = 1.0f - u_sum;
    L_c = u1 * PALETTE[idx1].L + u2 * PALETTE[idx2].L;
    float wW = 0, wK = 0;
    if (rem > 1e-5f) {
        float frac_W = (target_L - L_c - rem * k.L) / (rem * dL_kw);
        frac_W = frac_W < 0 ? 0 : frac_W > 1 ? 1 : frac_W;
        wW = rem * frac_W;
        wK = rem * (1.0f - frac_W);
    }

    float total = wK + wW + u1 + u2;
    if (total > 1e-5f) {
        float inv = 1.0f / total;
        wK *= inv; wW *= inv; u1 *= inv; u2 *= inv;
    } else {
        wW = 1.0f;
    }

    /* Compute Yule-Nielsen optical reflectance directly from the continuous mapped Lab target,
     * and blend slightly (20%) with the tetrahedral vertex combination so the target field is
     * 100% C1-continuous across hue sectors while staying inside the physical ink hull. */
    float fy = (mapped.L + 16.0f) / 116.0f;
    float fx = fy + mapped.a / 500.0f;
    float fz = fy - mapped.b / 200.0f;
    const float d = 6.0f / 29.0f;
    float X = (fx > d ? fx * fx * fx : 3.0f * d * d * (fx - 4.0f / 29.0f)) * 0.95047f;
    float Y = (fy > d ? fy * fy * fy : 3.0f * d * d * (fy - 4.0f / 29.0f));
    float Z = (fz > d ? fz * fz * fz : 3.0f * d * d * (fz - 4.0f / 29.0f)) * 1.08883f;
    float lr =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float lg = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float lb =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
    lr = lr < 0 ? 0 : lr > 1 ? 1 : lr;
    lg = lg < 0 ? 0 : lg > 1 ? 1 : lg;
    lb = lb < 0 ? 0 : lb > 1 ? 1 : lb;
    lin_t cont_lin = { powf(lr, YN_INV), powf(lg, YN_INV), powf(lb, YN_INV) };

    lin_t lk = PALETTE_LIN[INK_BLACK], lw = PALETTE_LIN[INK_WHITE];
    lin_t l1 = PALETTE_LIN[idx1], l2 = PALETTE_LIN[idx2];
    lin_t bary_lin = {
        wK * lk.r + wW * lw.r + u1 * l1.r + u2 * l2.r,
        wK * lk.g + wW * lw.g + u1 * l1.g + u2 * l2.g,
        wK * lk.b + wW * lw.b + u1 * l1.b + u2 * l2.b
    };
    lin_t mix = {
        0.75f * cont_lin.r + 0.25f * bary_lin.r,
        0.75f * cont_lin.g + 0.25f * bary_lin.g,
        0.75f * cont_lin.b + 0.25f * bary_lin.b
    };
    *out_lin = mix;
    *out_lab = mapped;

    uint8_t mask = 0;
    if (wK > 0.006f) mask |= (1u << INK_BLACK);
    if (wW > 0.006f) mask |= (1u << INK_WHITE);
    if (u1 > 0.005f) mask |= (1u << idx1);
    if (u2 > 0.005f) mask |= (1u << idx2);
    /* In bright highlights (e.g. skin, light sky), suppress black ink if white dominates. */
    if (t > 0.68f && wW > 0.22f && wK < 0.05f) {
        mask &= ~(1u << INK_BLACK);
    }
    if (!mask) mask = (1u << INK_WHITE) | (1u << INK_BLACK);
    *out_mask = mask;
}
