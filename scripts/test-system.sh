#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_qemu
PYTHON_BIN="${PYTHON_BIN:-$(find_tool python3)}"
exec "$PYTHON_BIN" "$ROOT_DIR/scripts/test-system.py" --qemu "$QEMU_BIN" \
  --build-dir "${BUILD_DIR:-$ROOT_DIR/build}"
