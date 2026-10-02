#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_qemu
PYTHON_BIN="${PYTHON_BIN:-$(find_tool python3)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
"$PYTHON_BIN" "$ROOT_DIR/scripts/test-ipc.py" --qemu "$QEMU_BIN" --build-dir "$BUILD_DIR"
