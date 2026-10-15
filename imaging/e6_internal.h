#pragma once
#include "e6.h"
#include <stdbool.h>

typedef struct { float L, a, b; } lab_t;
enum { INK_BLACK, INK_WHITE, INK_RED, INK_YELLOW, INK_BLUE, INK_GREEN, INKS };
enum { REG_FLAT, REG_EDGE, REG_DARK, REG_LIGHT, REG_SKIN, REG_SKY, REG_FOLIAGE, REGIONS };

typedef struct {
    float exposure, brightness, contrast, saturation, vibrance, temperature, tint, gamma;
    float clarity, sharpen, smoothness, chroma;
    int mode, refine;
} params_t;

extern lab_t PALETTE[INKS];

/* color.c */
void color_init(void);
float srgb_to_linear(float c);
float linear_to_srgb(float c);
lab_t rgb_to_lab(float r, float g, float b);
float de2000(lab_t x, lab_t y);
float de76_sq(lab_t x, lab_t y);
lab_t map_to_panel(lab_t source, float chroma);

/* resample.c */
void resample(const float *src, int sw, int sh, float *dst, int dw, int dh);

/* adjust.c (interleaved RGB floats in 0..1) */
void adjust_rgb(float *rgb, int count, const params_t *p);
void enhance_rgb(float *rgb, int w, int h, float clarity);
void unsharp_rgb(float *rgb, int w, int h, float amount);

/* dither_classic.c: shared error diffusion plus the classic picker. */
typedef int (*picker_t)(lab_t want, int x, int y, int w, const uint8_t *out, uint8_t region, float *chosen_de);
void diffuse(const lab_t *target, const float *edge, const uint8_t *region, int w, int h,
             float smoothness, picker_t pick, uint8_t *out);
int pick_classic(lab_t want, int x, int y, int w, const uint8_t *out, uint8_t region, float *chosen_de);
float tiebreak(int x, int y, int ink);
int same_neighbours(const uint8_t *out, int x, int y, int w, int ink);

/* dither_e6.c */
void classify(const lab_t *source, const float *edge, int count, uint8_t *region);
int pick_e6(lab_t want, int x, int y, int w, const uint8_t *out, uint8_t region, float *chosen_de);
float region_bias(uint8_t region, int ink);
void refine(const lab_t *target, const uint8_t *region, const float *edge, int w, int h, int passes, bool e6, uint8_t *out);
