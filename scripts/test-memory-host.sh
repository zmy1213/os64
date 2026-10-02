#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$HOST_CXX" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/page_allocator_host.cpp" \
  "$ROOT_DIR/kernel/memory/page_allocator.cpp" -o "$BUILD_DIR/memory-host-test"
"$BUILD_DIR/memory-host-test"
