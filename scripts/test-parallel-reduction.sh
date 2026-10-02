#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
resolve_qemu
"$PYTHON_BIN" "$ROOT_DIR/scripts/test-parallel-reduction.py" --qemu "$QEMU_BIN" --build-dir "${BUILD_DIR:-$ROOT_DIR/build}" "$@"
