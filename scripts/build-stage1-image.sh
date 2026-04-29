#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
STAGE1_SRC="$ROOT_DIR/boot/stage1.asm"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
DISK_IMG="$BUILD_DIR/disk.img"
IMAGE_SIZE=1474560

mkdir -p "$BUILD_DIR"

echo "[1/4] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

echo "[2/4] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none

echo "[3/4] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

echo "[4/4] build complete"
echo "stage1.bin: $STAGE1_BIN"
echo "disk.img:    $DISK_IMG"
echo "signature:   $signature"

