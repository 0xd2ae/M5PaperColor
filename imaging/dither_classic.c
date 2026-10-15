#include "e6_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Interleaved gradient noise (Jimenez 2014) + golden-ratio ink offset:
 * produces a blue-noise-like high-frequency spatial distribution without periodic Bayer grid lines. */
float tiebreak(int x, int y, int ink)
{
    float fx = (float)(x + ink * 17);
    float fy = (float)(y + ink * 31);
    float v = fmodf(52.9829189f * fmodf(0.06711056f * fx + 0.00583715f * fy, 1.0f), 1.0f);
    if (v < 0) v += 1.0f;
    return (v - 0.5f) * 0.42f;
}

/* Weighted causal neighbourhood repulsion: orthogonal neighbours count 2, diagonal neighbours count 1,
 * and 2-step orthogonal neighbours count 0.5 to encourage blue-noise minority-pixel spacing. */
int same_neighbours(const uint8_t *out, int x, int y, int w, int ink)
{
    int n = 0, prev = (y & 1) ? x + 1 : x - 1, prev2 = (y & 1) ? x + 2 : x - 2;
    if (prev >= 0 && prev < w && out[y * w + prev] == ink) n += 2;
    if (prev2 >= 0 && prev2 < w && out[y * w + prev2] == ink) n += 1;
    if (y > 0) {
        const uint8_t *up = out + (y - 1) * w;
        if (up[x] == ink) n += 2;
        if (x > 0 && up[x - 1] == ink) n += 1;
        if (x + 1 < w && up[x + 1] == ink) n += 1;
    }
    if (y > 1 && out[(y - 2) * w + x] == ink) n += 1;
    return n;
}

int pick_classic(lin_t want_lin, lab_t want_lab, int x, int y, int w, const uint8_t *out,
                 uint8_t region, uint8_t mask, float *chosen_de)
{
    (void)want_lin;
    (void)mask;
    int best = 0;
    float best_score = 1e9f, best_de = 0;
    float spread = region == REG_FLAT ? 0.38f : region == REG_EDGE ? 0.06f : 0.24f;
    for (int ink = 0; ink < INKS; ink++) {
        float de = de2000(want_lab, PALETTE[ink]);
        float score = de + same_neighbours(out, x, y, w, ink) * spread * fminf(1.0f, de / 6.0f) + tiebreak(x, y, ink);
        if (score < best_score) {
            best_score = score;
            best = ink;
            best_de = de;
        }
    }
    *chosen_de = best_de;
    return best;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Key nodes from Ostromoukhov's variable-coefficient error-diffusion table (SIGGRAPH 2001):
 * weights { right, down-left, down } interpolated across tonal density to break worm artifacts. */
static void ostromoukhov_weights(float level, float *w_r, float *w_dl, float *w_d)
{
    static const float K[9][3] = {
        { 13.0f, 0.0f,  5.0f }, /* 0.000: extreme shadow/highlight */
        { 13.0f, 0.0f,  5.0f }, /* 0.125 */
        { 21.0f, 3.0f, 10.0f }, /* 0.250 */
        {  7.0f, 3.0f,  6.0f }, /* 0.375 */
        {  8.0f, 4.0f,  4.0f }, /* 0.500: midtone */
        {  7.0f, 3.0f,  6.0f }, /* 0.625 */
        { 21.0f, 3.0f, 10.0f }, /* 0.750 */
        { 13.0f, 0.0f,  5.0f }, /* 0.875 */
        { 13.0f, 0.0f,  5.0f }  /* 1.000 */
    };
    float pos = clampf(level, 0.0f, 1.0f) * 8.0f;
    int idx = (int)pos;
    if (idx >= 8) idx = 7;
    float frac = pos - idx;
    float r  = K[idx][0] + frac * (K[idx + 1][0] - K[idx][0]);
    float dl = K[idx][1] + frac * (K[idx + 1][1] - K[idx][1]);
    float d  = K[idx][2] + frac * (K[idx + 1][2] - K[idx][2]);
    float sum = r + dl + d;
    *w_r  = r / sum;
    *w_dl = dl / sum;
    *w_d  = d / sum;
}

/* Error diffusion in Yule-Nielsen linear optical reflectance space:
 * - Accumulating error in linear reflectance ensures multi-ink halftones converge to the true
 *   physical optical average rather than darkening/desaturating in non-linear Lab.
 * - Blends Ostromoukhov variable 3-tap weights with a weak 2nd-row Sierra/Stucki tail in smooth
 *   regions, while contracting to a tight edge-stopped kernel on text and line art. */
void diffuse(const lin_t *target_lin, const float *edge, const uint8_t *region, const uint8_t *mask,
             int w, int h, float smoothness, picker_t pick, uint8_t *out)
{
    lin_t *rows[3];
    for (int i = 0; i < 3; i++) rows[i] = calloc(w + 4, sizeof(lin_t));
    if (!rows[0] || !rows[1] || !rows[2]) {
        for (int i = 0; i < 3; i++) free(rows[i]);
        return;
    }

    float threshold = smoothness * 0.36f, hard = threshold * 0.48f;
    lin_t k_lin = PALETTE_LIN[INK_BLACK], w_lin = PALETTE_LIN[INK_WHITE];
    float min_r = fminf(k_lin.r, 0.18f), max_r = fmaxf(w_lin.r, 0.72f);
    float min_g = fminf(k_lin.g, 0.18f), max_g = fmaxf(w_lin.g, 0.72f);
    float min_b = fminf(k_lin.b, 0.18f), max_b = fmaxf(w_lin.b, 0.72f);

    for (int y = 0; y < h; y++) {
        bool reverse = y & 1;
        for (int step = 0; step < w; step++) {
            int x = reverse ? w - 1 - step : step, i = y * w + x, dir = reverse ? -1 : 1;
            lin_t e = rows[0][x + 2], t = target_lin[i];
            uint8_t reg = region[i], m = mask ? mask[i] : 0x3Fu;

            /* Tighter error clamp on edges and locked paper backgrounds so strokes never bleed. */
            float max_err = reg == REG_EDGE ? 0.14f : reg == REG_LIGHT ? 0.16f : 0.24f;
            lin_t want_lin = {
                clampf(t.r + clampf(e.r, -max_err, max_err), min_r - 0.05f, max_r + 0.05f),
                clampf(t.g + clampf(e.g, -max_err, max_err), min_g - 0.05f, max_g + 0.05f),
                clampf(t.b + clampf(e.b, -max_err, max_err), min_b - 0.05f, max_b + 0.05f)
            };
            lab_t want_lab = lin_to_lab(want_lin);

            float de = 0;
            int ink = pick(want_lin, want_lab, x, y, w, out, reg, m, &de);
            out[i] = (uint8_t)ink;

            /* Smoothness snap: near-exact matches stop propagating residual quantization noise. */
            float factor = 1.0f;
            if (threshold > 0) {
                factor = de <= hard ? 0.0f : de >= threshold ? 1.0f : (de - hard) / (threshold - hard);
            }
            /* Pure paper white / deep ink black with single-ink mask stops diffusing immediately. */
            if ((m & (m - 1)) == 0 && pick != pick_classic) factor = 0.0f;

            lin_t p_lin = PALETTE_LIN[ink];
            lin_t err = {
                (want_lin.r - p_lin.r) * factor,
                (want_lin.g - p_lin.g) * factor,
                (want_lin.b - p_lin.b) * factor
            };

            /* Normalized luminance level in panel range for Ostromoukhov coefficient lookup. */
            float lum = 0.2126f * t.r + 0.7152f * t.g + 0.0722f * t.b;
            float lum_k = 0.2126f * k_lin.r + 0.7152f * k_lin.g + 0.0722f * k_lin.b;
            float lum_w = 0.2126f * w_lin.r + 0.7152f * w_lin.g + 0.0722f * w_lin.b;
            float level = (lum - lum_k) / fmaxf(0.05f, lum_w - lum_k);
            float w_r, w_dl, w_d;
            ostromoukhov_weights(level, &w_r, &w_dl, &w_d);

            /* Blend Ostromoukhov 3-tap kernel (75%) with wide Sierra/Stucki tail (25%) in smooth areas;
             * contract to pure compact 1-row kernel on strong edges (text / line art). */
            float smooth_tail = clampf(1.0f - (edge[i] - 12.0f) / 42.0f, 0.0f, 1.0f) * 0.24f;
            float core_scale = 1.0f - smooth_tail;

            /* Immediate Ostromoukhov / Floyd-Steinberg taps: (x+dir, y), (x-dir, y+1), (x, y+1), (x+dir, y+1). */
            float f_r  = core_scale * w_r  + smooth_tail * (5.0f / 32.0f);
            float f_dl = core_scale * w_dl + smooth_tail * (4.0f / 32.0f);
            float f_d  = core_scale * w_d  + smooth_tail * (5.0f / 32.0f);
            float f_dr = smooth_tail * (4.0f / 32.0f);
            float f_r2 = smooth_tail * (3.0f / 32.0f);
            float f_dl2 = smooth_tail * (2.0f / 32.0f);
            float f_dr2 = smooth_tail * (2.0f / 32.0f);
            float f_2d  = smooth_tail * (3.0f / 32.0f);
            float f_2dl = smooth_tail * (2.0f / 32.0f);
            float f_2dr = smooth_tail * (2.0f / 32.0f);

            #define ADD_ERR(row_idx, xx, weight) do { \
                int _x = (xx); \
                if (_x >= 0 && _x < w && (weight) > 0.0f) { \
                    /* Attenuate error crossing into a strong edge or clean paper region */ \
                    lin_t *_d = &rows[row_idx][_x + 2]; \
                    _d->r += err.r * (weight); \
                    _d->g += err.g * (weight); \
                    _d->b += err.b * (weight); \
                } \
            } while (0)

            ADD_ERR(0, x + dir,     f_r);
            ADD_ERR(0, x + 2 * dir, f_r2);
            ADD_ERR(1, x - 2 * dir, f_dl2);
            ADD_ERR(1, x - dir,     f_dl);
            ADD_ERR(1, x,           f_d);
            ADD_ERR(1, x + dir,     f_dr);
            ADD_ERR(1, x + 2 * dir, f_dr2);
            ADD_ERR(2, x - dir,     f_2dl);
            ADD_ERR(2, x,           f_2d);
            ADD_ERR(2, x + dir,     f_2dr);
            #undef ADD_ERR
        }
        lin_t *done = rows[0];
        rows[0] = rows[1];
        rows[1] = rows[2];
        rows[2] = done;
        memset(rows[2], 0, sizeof(lin_t) * (w + 4));
    }
    for (int i = 0; i < 3; i++) free(rows[i]);
}
