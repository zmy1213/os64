#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$CLANGXX_BIN" -std=c++17 -O1 -g -fsanitize=address,undefined \
  -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/shell_parser_host.cpp" \
  "$ROOT_DIR/kernel/shell/parser.cpp" -o "$BUILD_DIR/shell-parser-host"
"$BUILD_DIR/shell-parser-host"
