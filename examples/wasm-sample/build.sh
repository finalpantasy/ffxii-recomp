#!/bin/sh
# Builds count.wasm with clang's wasm32 target (no libc needed).
# Usage: sh build.sh [clang]
set -e
cd "$(dirname "$0")"
CLANG=${1:-clang}
"$CLANG" --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export-dynamic \
  -I ../../include count.c -o count.wasm
