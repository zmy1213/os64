#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
resolve_qemu
printf 'os64 toolchain\n'
for name in NASM_BIN CLANGXX_BIN LD_BIN OBJCOPY_BIN PYTHON_BIN QEMU_BIN; do
  printf '[PASS] %s: %s\n' "$name" "${!name}"
done
printf '[PASS] Ready to build and run x86_64 freestanding images.\n'
