#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint8_t w, h, advance;
    int8_t x, y;
    uint16_t runs;
    const uint8_t *bitmap; /* RLE: bit 7 ink, bits 0-6 run length */
} pc_glyph_t;
/* Card font packs <directory>/font<size>.bin share the built-in PCF1 format. NULL closes them. */
void pc_fonts_open(const char *directory);
bool pc_fonts_available(int size);
/* Uncached file lookup; safe to call from another task while rendering. */
bool pc_fonts_has(int size, uint32_t codepoint);
/* Cached; render task only. The bitmap stays valid until the next lookup. */
bool pc_fonts_glyph(int size, uint32_t codepoint, pc_glyph_t *out);
