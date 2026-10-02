#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$HOST_CXX" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/network_host.cpp" \
  "$ROOT_DIR/kernel/net/network.cpp" "$ROOT_DIR/kernel/runtime/runtime.cpp" \
  -o "$BUILD_DIR/network-host-test"
"$BUILD_DIR/network-host-test"
"$HOST_CXX" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/network_irq_host.cpp" \
  "$ROOT_DIR/kernel/net/network_irq.cpp" -o "$BUILD_DIR/network-irq-host-test"
"$BUILD_DIR/network-irq-host-test"
