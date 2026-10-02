#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$HOST_CXX" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DOS64_LOG_HOST_TEST -I "$ROOT_DIR/kernel" -I "$ROOT_DIR/user" \
  "$ROOT_DIR/tests/log_host.cpp" "$ROOT_DIR/kernel/log/log.cpp" -o "$BUILD_DIR/log-host-test"
"$BUILD_DIR/log-host-test"
