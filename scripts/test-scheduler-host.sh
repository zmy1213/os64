#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
FLAGS=( -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  -ffunction-sections -fdata-sections -I "$ROOT_DIR/kernel" )
if [ "$(uname -s)" = Darwin ]; then
  # The included production TU contains x86 instructions in unreachable code.
  # Mac executes the x86 test through its installed translation support.
  FLAGS+=( -arch x86_64 -Wl,-dead_strip )
else
  FLAGS+=( -Wl,--gc-sections )
fi
"$HOST_CXX" "${FLAGS[@]}" "$ROOT_DIR/tests/scheduler_queue_host.cpp" -o "$BUILD_DIR/scheduler-host-test"
"$BUILD_DIR/scheduler-host-test"
