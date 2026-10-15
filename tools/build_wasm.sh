#!/bin/sh
# Build the browser image pipeline (imaging/*.c) into main/web/e6.wasm, embedded in the firmware.
# Requires Emscripten (brew install emscripten). Standalone WASM: no JS glue, app.js instantiates it.
set -e
cd "$(dirname "$0")/.."
emcc -O3 -flto -DNDEBUG \
    -sSTANDALONE_WASM=1 --no-entry \
    -sEXPORTED_FUNCTIONS=_e6_init,_e6_process,_e6_adjust,_e6_score,_e6_pack,_e6_set_palette_rgb,_e6_palette_rgb,_malloc,_free \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 -sMAXIMUM_MEMORY=268435456 \
    -Iimaging imaging/color.c imaging/resample.c imaging/adjust.c imaging/dither_classic.c imaging/dither_e6.c imaging/e6.c \
    -o main/web/e6.wasm
echo "wrote main/web/e6.wasm ($(wc -c < main/web/e6.wasm) bytes)"
