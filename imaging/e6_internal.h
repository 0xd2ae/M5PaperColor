#pragma once
#include "e6.h"
#include <stdbool.h>

typedef struct { float L, a, b; } lab_t;
typedef struct { float r, g, b; } lin_t;
enum { INK_BLACK, INK_WHITE, INK_RED, INK_YELLOW, INK_BLUE, INK_GREEN, INKS };
enum { REG_FLAT, REG_EDGE, REG_DARK, REG_LIGHT, REG_SKIN, REG_SKY, REG_FOLIAGE, REGIONS };

typedef struct {
    float exposure, brightness, contrast, saturation, vibrance, temperature, tint, gamma;
    float clarity, sharpen, smoothness, chroma;
    int mode, refine;
} params_t;

extern lab_t PALETTE[INKS];
extern lin_t PALETTE_LIN[INKS];

/* color.c */
void color_init(void);
float srgb_to_linear(float c);
float linear_to_srgb(float c);
lin_t srgb_to_optical(float r, float g, float b);
lab_t lin_to_lab(lin_t c);
lab_t rgb_to_lab(float r, float g, float b);
float de2000(lab_t x, lab_t y);
float de76_sq(lab_t x, lab_t y);
lab_t map_to_panel(lab_t source, float chroma);
void decompose_to_panel(lab_t source, float chroma, lin_t *out_lin, lab_t *out_lab, uint8_t *out_mask);

/* resample.c */
void resample(const float *src, int sw, int sh, float *dst, int dw, int dh);

/* adjust.c (interleaved RGB floats in 0..1) */
void adjust_rgb(float *rgb, int count, const params_t *p);
void enhance_rgb(float *rgb, int w, int h, float clarity);
void unsharp_rgb(float *rgb, int w, int h, float amount);

/* dither_classic.c: shared error diffusion plus the classic picker. */
typedef int (*picker_t)(lin_t want_lin, lab_t want_lab, int x, int y, int w, const uint8_t *out,
                        uint8_t region, uint8_t mask, float *chosen_de);
void diffuse(const lin_t *target_lin, const float *edge, const uint8_t *region, const uint8_t *mask,
             int w, int h, float smoothness, picker_t pick, uint8_t *out);
int pick_classic(lin_t want_lin, lab_t want_lab, int x, int y, int w, const uint8_t *out,
                 uint8_t region, uint8_t mask, float *chosen_de);
float tiebreak(int x, int y, int ink);
int same_neighbours(const uint8_t *out, int x, int y, int w, int ink);

/* dither_e6.c */
void classify(const lab_t *source, const float *edge, int w, int h, uint8_t *region, uint8_t *mask);
int pick_e6(lin_t want_lin, lab_t want_lab, int x, int y, int w, const uint8_t *out,
            uint8_t region, uint8_t mask, float *chosen_de);
float region_bias(uint8_t region, int ink);
void refine(const lin_t *target_lin, const lab_t *target_lab, const uint8_t *region, const uint8_t *mask,
            const float *edge, int w, int h, int passes, bool e6, uint8_t *out);
