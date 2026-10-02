#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_qemu
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
ARGS=( -m 128M -boot a
  -drive "format=raw,file=$BUILD_DIR/disk.img,if=floppy,index=0"
  -drive "format=raw,file=$BUILD_DIR/data.img,if=ide,index=0"
  -serial stdio -monitor none
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 )
if [ "${1:-}" != --gui ]; then
  ARGS+=( -display none )
fi
exec "$QEMU_BIN" "${ARGS[@]}"
