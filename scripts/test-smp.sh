#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
resolve_qemu
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
"$PYTHON_BIN" "$ROOT_DIR/scripts/test-smp.py" --qemu "$QEMU_BIN" --build-dir "$BUILD_DIR"
