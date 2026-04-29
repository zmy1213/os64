#!/usr/bin/env bash
set -euo pipefail

# Resolve the repo root once so the script works from any current directory.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
STAGE1_SRC="$ROOT_DIR/boot/stage1.asm"
STAGE2_SRC="$ROOT_DIR/boot/stage2.asm"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
STAGE2_BIN="$BUILD_DIR/stage2.bin"
DISK_IMG="$BUILD_DIR/disk.img"
IMAGE_SIZE=1474560

# Build outputs live under build/ so the repo stays clean.
mkdir -p "$BUILD_DIR"

# Stage1 must remain a single 512-byte BIOS boot sector.
echo "[1/5] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# Stage2 also stays one sector for the first two-stage loading milestone.
echo "[2/5] assembling stage2"
nasm -f bin "$STAGE2_SRC" -o "$STAGE2_BIN"

size="$(wc -c < "$STAGE2_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage2 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# Create a raw floppy-sized image and place stage1/stage2 in the first sectors.
echo "[3/5] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
dd if="$STAGE2_BIN" of="$DISK_IMG" bs=512 seek=1 count=1 conv=notrunc status=none

# BIOS boot sectors must end with the 0x55aa signature.
echo "[4/5] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

# Emit the key output paths so manual runs can inspect the artifacts directly.
echo "[5/5] build complete"
echo "stage1.bin: $STAGE1_BIN"
echo "stage2.bin: $STAGE2_BIN"
echo "disk.img:    $DISK_IMG"
echo "signature:   $signature"
