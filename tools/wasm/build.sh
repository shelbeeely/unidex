#!/bin/sh
# Builds site/x4/house.wasm from src/apps/house/house_logic.h (via tools/wasm/house_wasm.cpp) with clang alone:
# freestanding wasm32, the tiny libc in tools/wasm/libc. Needs clang and wasm-ld (lld), version 15 or newer.
# Usage: tools/wasm/build.sh   (from the repo root; CI checks that the committed file is up to date)
set -eu
cd "$(dirname "$0")/../.."
CLANG="${CLANG:-clang}"
inc="$($CLANG -print-resource-dir)/include"
out="${1:-site/x4/house.wasm}"
tmp="${TMPDIR:-/tmp}/unidex-wasm"
mkdir -p "$tmp" "$(dirname "$out")"
flags="--target=wasm32 -O2 -nostdinc -isystem $inc -isystem tools/wasm/libc -fno-exceptions -fno-rtti -ffreestanding -fvisibility=hidden -fno-builtin -Wall"
$CLANG $flags -c tools/wasm/libc/libc.c -o "$tmp/libc.o"
$CLANG $flags -std=c++17 -I src/apps/house -c tools/wasm/house_wasm.cpp -o "$tmp/house.o"
wasm-ld --no-entry --export-dynamic --strip-all --lto-O2 -z stack-size=262144 --initial-memory=4194304 \
  "$tmp/house.o" "$tmp/libc.o" -o "$out"
echo "$out: $(wc -c < "$out") bytes"
