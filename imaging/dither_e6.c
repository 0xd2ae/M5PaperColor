#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>

/* Visibility penalties (CIEDE2000 units) per region and ink: black, white, red, yellow, blue, green.
 * Dark/red/blue dots on light paper and cool dots in skin read as dirt far more than their Lab error suggests. */
static const float BIAS[REGIONS][INKS] = {
    [REG_FLAT]    = { 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    [REG_EDGE]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    [REG_DARK]    = { 0.0f, 3.5f, 4.0f, 5.0f, 4.0f, 4.0f },
    [REG_LIGHT]   = { 10.0f, 0.0f, 6.0f, 1.2f, 6.0f, 3.5f },
    [REG_SKIN]    = { 2.5f, 0.0f, 0.0f, 0.0f, 8.0f, 8.0f },
    [REG_SKY]     = { 1.5f, 0.0f, 7.0f, 4.5f, 0.0f, 2.5f },
    [REG_FOLIAGE] = { 1.2f, 0.0f, 8.0f, 0.0f, 2.5f, 0.0f },
};
float region_bias(uint8_t region, int ink) { return BIAS[region][ink]; }

/* Scene & structure classification on unmapped source Lab:
 * - Identifies strong edges (text strokes, anime line art), pure paper highlights, deep shadows,
 *   skin tones, sky, and foliage.
 * - Uses continuous tetrahedral barycentric weights from decompose_to_panel() as the primary
 *   compatible-ink mask, only snapping extreme paper white / ink black so continuous gradients
 *   never exhibit hard region-boundary contours. */
void classify(const lab_t *s, const float *edge, int w, int h, uint8_t *region, uint8_t *mask)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            float c = sqrtf(s[i].a * s[i].a + s[i].b * s[i].b);
            float hue = atan2f(s[i].b, s[i].a) * 57.29578f;
            if (hue < 0) hue += 360.0f;

            uint8_t r = REG_FLAT;
            if (edge[i] > 36.0f) r = REG_EDGE;
            else if (s[i].L < 22.0f && c < 14.0f) r = REG_DARK;
            else if (s[i].L > 78.0f && c < 16.0f) r = REG_LIGHT;
            else if (c >= 10.0f && c <= 55.0f && hue >= 15.0f && hue <= 75.0f && s[i].L >= 35.0f && s[i].L <= 88.0f) r = REG_SKIN;
            else if (c >= 8.0f && hue >= 200.0f && hue <= 280.0f) r = REG_SKY;
            else if (c >= 10.0f && hue >= 95.0f && hue <= 170.0f) r = REG_FOLIAGE;
            region[i] = r;

            if (!mask) continue;

            /* Pure paper white highlight anchoring (documents, manga margins, specular white). */
            if (s[i].L > 93.0f && c < 5.0f && edge[i] < 16.0f) {
                mask[i] = (1u << INK_WHITE);
                continue;
            }
            /* Deep shadow / black ink stroke anchoring. */
            if (s[i].L < 12.0f && c < 8.0f) {
                mask[i] = (1u << INK_BLACK);
                continue;
            }
            /* Very light neutral paper: forbid black, red, and blue confetti dots. */
            if (r == REG_LIGHT && s[i].L > 84.0f && c < 9.0f) {
                uint8_t m = mask[i] & ~((1u << INK_BLACK) | (1u << INK_RED) | (1u << INK_BLUE));
                mask[i] = m ? m : (1u << INK_WHITE);
            }
        }
    }
}

static float spread_for(uint8_t region, int ink)
{
    if (region == REG_LIGHT && ink == INK_WHITE) return 0.0f;
    if (region == REG_DARK && ink == INK_BLACK) return 0.0f;
    /* Edges allow consecutive same-ink pixels so text strokes and anime outlines stay solid. */
    return region == REG_FLAT ? 0.42f : region == REG_EDGE ? 0.04f : 0.28f;
}

int pick_e6(lin_t want_lin, lab_t want_lab, int x, int y, int w, const uint8_t *out,
            uint8_t region, uint8_t mask, float *chosen_de)
{
    if (!mask) mask = 0x3Fu;

    float de[INKS], lin_dist[INKS];
    int first = -1, second = -1;
    for (int ink = 0; ink < INKS; ink++) {
        de[ink] = de2000(want_lab, PALETTE[ink]);
        lin_t p = PALETTE_LIN[ink];
        float dr = want_lin.r - p.r, dg = want_lin.g - p.g, db = want_lin.b - p.b;
        /* Optical reflectance distance weighted by human luminous efficiency. */
        lin_dist[ink] = sqrtf(0.25f * dr * dr + 0.55f * dg * dg + 0.20f * db * db) * 28.0f;
        if (!(mask & (1u << ink))) continue;
        if (first < 0 || de[ink] < de[first]) {
            second = first;
            first = ink;
        } else if (second < 0 || de[ink] < de[second]) {
            second = ink;
        }
    }
    if (first < 0) first = INK_WHITE;
    if (second < 0) second = first;

    int best = first;
    float best_score = 1e9f;
    for (int ink = 0; ink < INKS; ink++) {
        if (!(mask & (1u << ink))) continue;
        /* Bias only settles close calls; a clear winner keeps its colour. */
        float bias = (ink == first && de[second] - de[first] > 9.5f) ? 0.0f : BIAS[region][ink];
        float metric = 0.72f * de[ink] + 0.28f * lin_dist[ink];
        float repulse = same_neighbours(out, x, y, w, ink) * spread_for(region, ink) * fminf(1.0f, de[ink] / 6.0f);
        float score = metric + repulse + bias + tiebreak(x, y, ink);
        if (score < best_score) {
            best_score = score;
            best = ink;
        }
    }
    *chosen_de = de[best];
    return best;
}

static int cluster(const uint8_t *out, int x, int y, int w, int h, int ink)
{
    int n = 0;
    if (x > 0 && out[y * w + x - 1] == ink) n += 2;
    if (x + 1 < w && out[y * w + x + 1] == ink) n += 2;
    if (y > 0 && out[(y - 1) * w + x] == ink) n += 2;
    if (y + 1 < h && out[(y + 1) * w + x] == ink) n += 2;
    if (x > 0 && y > 0 && out[(y - 1) * w + x - 1] == ink) n += 1;
    if (x + 1 < w && y > 0 && out[(y - 1) * w + x + 1] == ink) n += 1;
    if (x > 0 && y + 1 < h && out[(y + 1) * w + x - 1] == ink) n += 1;
    if (x + 1 < w && y + 1 < h && out[(y + 1) * w + x + 1] == ink) n += 1;
    return n;
}

static float cluster_weight(uint8_t region, int ink)
{
    if (region == REG_LIGHT && ink == INK_WHITE) return 0.0f;
    if (region == REG_DARK && ink == INK_BLACK) return 0.0f;
    return region == REG_FLAT ? 0.14f : region == REG_EDGE ? 0.01f : 0.08f;
}

/* 5x5 Human Visual System (HVS) point-spread weights:
 * - Narrow Gaussian (sigma ~ 0.90) for luminance (detail & edge sharpness).
 * - Broader Gaussian (sigma ~ 1.35) for chrominance (retinal colour fusion). */
static const float HVS_L[5][5] = {
    { 0.003f, 0.013f, 0.022f, 0.013f, 0.003f },
    { 0.013f, 0.059f, 0.097f, 0.059f, 0.013f },
    { 0.022f, 0.097f, 0.159f, 0.097f, 0.022f },
    { 0.013f, 0.059f, 0.097f, 0.059f, 0.013f },
    { 0.003f, 0.013f, 0.022f, 0.013f, 0.003f }
};
static const float HVS_C[5][5] = {
    { 0.012f, 0.026f, 0.034f, 0.026f, 0.012f },
    { 0.026f, 0.058f, 0.075f, 0.058f, 0.026f },
    { 0.034f, 0.075f, 0.098f, 0.075f, 0.034f },
    { 0.026f, 0.058f, 0.075f, 0.058f, 0.026f },
    { 0.012f, 0.026f, 0.034f, 0.026f, 0.012f }
};

/* Direct Binary Search (DBS-lite) halftone refinement with HVS dual-band Gaussian filter:
 * Evaluates local optical & perceptual mixture in a 5x5 HVS window and updates pixels
 * within their compatible tetrahedral ink set when the perceived error strictly decreases. */
void refine(const lin_t *target_lin, const lab_t *target_lab, const uint8_t *region, const uint8_t *mask,
            const float *edge, int w, int h, int passes, bool e6, uint8_t *out)
{
    if (passes <= 0) return;
    (void)target_lin;
    const int R = 2;
    int n_pix = w * h;

    /* Precompute HVS-filtered target (tL, ta, tb) at each pixel so passes are fast O(25) inner loops. */
    lab_t *hvs_target = malloc(sizeof(lab_t) * n_pix);
    if (!hvs_target) return;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int x0 = x - R < 0 ? 0 : x - R, x1 = x + R >= w ? w - 1 : x + R;
            int y0 = y - R < 0 ? 0 : y - R, y1 = y + R >= h ? h - 1 : y + R;
            float sum_wL = 0, sum_wC = 0, tL = 0, ta = 0, tb = 0;
            for (int yy = y0; yy <= y1; yy++) {
                for (int xx = x0; xx <= x1; xx++) {
                    float wL = HVS_L[yy - y + R][xx - x + R];
                    float wC = HVS_C[yy - y + R][xx - x + R];
                    const lab_t *t = &target_lab[yy * w + xx];
                    tL += wL * t->L;
                    ta += wC * t->a;
                    tb += wC * t->b;
                    sum_wL += wL;
                    sum_wC += wC;
                }
            }
            hvs_target[y * w + x] = (lab_t){ tL / sum_wL, ta / sum_wC, tb / sum_wC };
        }
    }

    for (int pass = 0; pass < passes; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int i = y * w + x;
                uint8_t m = (e6 && mask) ? mask[i] : 0x3Fu;
                /* Single-ink anchored pixel (e.g. pure paper white or black text) needs no refinement. */
                if ((m & (m - 1)) == 0) continue;

                int x0 = x - R < 0 ? 0 : x - R, x1 = x + R >= w ? w - 1 : x + R;
                int y0 = y - R < 0 ? 0 : y - R, y1 = y + R >= h ? h - 1 : y + R;
                float sum_wL = 0, sum_wC = 0, L = 0, A = 0, B = 0;
                for (int yy = y0; yy <= y1; yy++) {
                    for (int xx = x0; xx <= x1; xx++) {
                        float wL = HVS_L[yy - y + R][xx - x + R];
                        float wC = HVS_C[yy - y + R][xx - x + R];
                        const lab_t *p = &PALETTE[out[yy * w + xx]];
                        L += wL * p->L;
                        A += wC * p->a;
                        B += wC * p->b;
                        sum_wL += wL;
                        sum_wC += wC;
                    }
                }
                float wL0 = HVS_L[R][R] / sum_wL;
                float wC0 = HVS_C[R][R] / sum_wC;
                L /= sum_wL; A /= sum_wC; B /= sum_wC;

                lab_t ht = hvs_target[i];
                int old = out[i], best = old;
                float d_old = de2000(target_lab[i], PALETTE[old]), best_cost = 0;
                /* Stronger self-pixel fidelity on sharp edges (text/line art) so strokes stay crisp. */
                float self_w = edge[i] > 28.0f ? 0.08f : 0.015f;

                for (int k = -1; k < INKS; k++) {
                    int ink = k < 0 ? old : k;
                    if (k >= 0) {
                        if (ink == old || !(m & (1u << ink))) continue;
                        if (de2000(target_lab[i], PALETTE[ink]) > d_old + 11.0f) continue;
                    }
                    float mL = L + (PALETTE[ink].L - PALETTE[old].L) * wL0 - ht.L;
                    float ma = A + (PALETTE[ink].a - PALETTE[old].a) * wC0 - ht.a;
                    float mb = B + (PALETTE[ink].b - PALETTE[old].b) * wC0 - ht.b;
                    float self_de = de76_sq(target_lab[i], PALETTE[ink]) * self_w;
                    float cost = 1.35f * mL * mL + 0.65f * (ma * ma + mb * mb) + self_de
                               + cluster(out, x, y, w, h, ink) * cluster_weight(region[i], ink);
                    if (e6) cost += region_bias(region[i], ink) * 0.14f;
                    /* Hysteresis: only swap for a genuine perceptual improvement. */
                    if (k < 0) best_cost = cost - 0.045f;
                    else if (cost < best_cost) { best_cost = cost; best = ink; }
                }
                out[i] = (uint8_t)best;
            }
        }
    }
    free(hvs_target);
}
