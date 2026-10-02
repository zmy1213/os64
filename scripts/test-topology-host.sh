#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$HOST_CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/topology_host.cpp" \
  "$ROOT_DIR/kernel/cpu/topology.cpp" -o "$BUILD_DIR/topology-host-test"
"$BUILD_DIR/topology-host-test"
