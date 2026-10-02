#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_qemu
PYTHON_BIN="${PYTHON_BIN:-$(find_tool python3)}"
"$PYTHON_BIN" "$ROOT_DIR/scripts/test-memory-user.py" --qemu "$QEMU_BIN" --build-dir "$ROOT_DIR/build"
