#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
resolve_qemu
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BENCHMARK_ASSETS="${BENCHMARK_ASSETS:-$ROOT_DIR/build/benchmark-deps}"
# Linux assets are intentionally explicit; ordinary tests never download them.
BUILD_DIR="$BUILD_DIR" BENCHMARK_ASSETS="$BENCHMARK_ASSETS" bash "$ROOT_DIR/scripts/build-linux-bench.sh"
"$PYTHON_BIN" "$ROOT_DIR/scripts/test-performance.py" --qemu "$QEMU_BIN" \
  --build-dir "$BUILD_DIR" --assets "$BENCHMARK_ASSETS" --compare-linux \
  --samples "${BENCHMARK_SAMPLES:-31}" --cpus "${BENCHMARK_CPUS:-1}"
