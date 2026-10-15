#pragma once
/* Six-colour E Ink image pipeline, compiled to WebAssembly for the setup page and natively for host checks.
 * Palette order matches pc_color_t: black, white, red, yellow, blue, green. */
#include <stdint.h>

enum {
    E6_EXPOSURE, E6_BRIGHTNESS, E6_CONTRAST, E6_SATURATION, E6_VIBRANCE, E6_TEMPERATURE, E6_TINT,
    E6_GAMMA, E6_CLARITY, E6_SHARPEN, E6_SMOOTHNESS, E6_MODE, E6_CHROMA, E6_REFINE, E6_PARAM_COUNT
};
enum { E6_MODE_MIX, E6_MODE_CLASSIC };

void e6_init(void);
/* Measured panel colours as 18 sRGB bytes; defaults are used until this is called. */
void e6_set_palette_rgb(const uint8_t *rgb);
void e6_palette_rgb(uint8_t *rgb);
/* rgba: w x h source at 1.5x the output size, already rotated and cropped.
 * params: E6_PARAM_COUNT floats. out: ow x oh palette indices. Returns 0 on success. */
int e6_process(const uint8_t *rgba, int w, int h, const float *params, uint8_t *out, int ow, int oh);
/* Applies only the global tone/colour adjustments in place (alpha untouched), to build a scoring reference. */
void e6_adjust(uint8_t *rgba, int w, int h, const float *params);
/* Lower is better: panel-optics fidelity of indices against the reference image rgba. */
float e6_score(const uint8_t *rgba, int w, int h, const uint8_t *indices, int ow, int oh);
/* Two pixels per byte, high nibble first. */
void e6_pack(const uint8_t *indices, int count, uint8_t *packed);
