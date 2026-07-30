#!/bin/sh
# Web build: compiles the Qt-free emulator core with Emscripten and
# bundles the browser frontend. Run from the repository root:
#   sh web/build.sh
# Output lands in web/dist/.
set -e

EMCC="${EMCC:-emcc}"
OUT=web/dist

mkdir -p "$OUT"

# spdlog runs header-only here (no SPDLOG_COMPILED_LIB), so only its
# include directory is needed
"$EMCC" -O3 -std=c++20 \
    src/core/*.cpp src/machine/*.cpp web/wasm_main.cpp \
    -Isrc \
    -Imodules/spdlog/include \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createDmwss \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_dmwss_load_rom,_dmwss_run_frame,_dmwss_framebuffer,_dmwss_set_joypad,_dmwss_read_audio,_dmwss_rom_loaded,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8 \
    -o "$OUT/dmwss.js"

cp web/index.html "$OUT/"

echo "Web build ready in $OUT/"
