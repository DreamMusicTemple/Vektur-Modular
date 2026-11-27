#!/bin/bash
# build_web.sh — builds the browser test version of Vektur Modular:
# the real C++ kernel compiled to WebAssembly (no Emscripten needed: clang's
# wasm32 target + wasm-ld + web/runtime/wasm_rt.cpp), plus the parameter
# table. Output: web/kernel.wasm (and web/params.json from tools/params.py).
#
#   tools/build_web.sh          # then serve web/ (python3 -m http.server -d web)
#
# Needs clang ≥ 16 with the wasm32 target and wasm-ld. Uses the host's
# libstdc++ headers (header-only parts); two target shims live in web/runtime/include.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DSP="$ROOT/Packages/VekturKit/Sources/VekturDSP"
OUT="$ROOT/web"
OBJ="$(mktemp -d)"
trap 'rm -rf "$OBJ"' EXIT

CXXV=$(ls /usr/include/c++ | sort -V | tail -1)
ARCH=$(ls -d /usr/include/*-linux-gnu 2>/dev/null | head -1)
# Shims: no 32-bit glibc stubs; no i386 regparm on pthread cleanup functions.
SHIM="$OUT/runtime/include"
mkdir -p "$SHIM/gnu" "$SHIM/bits"
: > "$SHIM/gnu/stubs-32.h"
sed 's/# define __cleanup_fct_attribute __attribute__ ((__regparm__ (1)))/# define __cleanup_fct_attribute/' \
    "$ARCH/bits/pthreadtypes-arch.h" > "$SHIM/bits/pthreadtypes-arch.h"

FLAGS=(--target=wasm32 -std=c++20 -O3 -fno-exceptions -fno-rtti -fno-threadsafe-statics -nostdlib -DNDEBUG -DVK_WEB=1
       -mbulk-memory -msimd128 -mnontrapping-fptoint -msign-ext
       -I"$SHIM" -isystem "/usr/include/c++/$CXXV" -isystem "$ARCH/c++/$CXXV" -isystem /usr/include -isystem "$ARCH"
       -I"$DSP" -I"$DSP/include")

python3 "$ROOT/tools/params.py" > /dev/null
SOURCES=("$DSP/Kernel.cpp" "$DSP/KernelMidi.cpp" "$DSP/CAPI.cpp" "$DSP"/graph/*.cpp "$DSP"/seq/*.cpp "$DSP"/fx/*.cpp "$OUT/runtime/vk_web.cpp")
i=0
for f in "${SOURCES[@]}"; do o="$OBJ/$i.o"; i=$((i + 1)); clang++ "${FLAGS[@]}" -c "$f" -o "$o" & done
clang++ "${FLAGS[@]}" -fno-builtin -c "$OUT/runtime/wasm_rt.cpp" -o "$OBJ/rt.o" &
wait

EXPORTS=()
for s in $(grep -oE '\bvk_[a-z0-9_]+\s*\(' "$DSP/include/VekturDSP.h" | sed 's/\s*(//' | sort -u | grep -v '^vk_au_'); do EXPORTS+=("--export=$s"); done
wasm-ld --no-entry --strip-all --gc-sections -z stack-size=1048576 --initial-memory=33554432 --max-memory=1073741824 \
    "${EXPORTS[@]}" --export=__heap_base -o "$OUT/kernel.wasm" "$OBJ"/*.o
echo "web/kernel.wasm $(wc -c < "$OUT/kernel.wasm") bytes"
