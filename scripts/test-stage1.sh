#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/stage1.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"

rm -f "$SERIAL_LOG"

"$QEMU_BIN" \
  -drive format=raw,file="$DISK_IMG" \
  -display none \
  -monitor none \
  -serial "file:$SERIAL_LOG" \
  -no-reboot \
  -no-shutdown &

qemu_pid="$!"
trap 'kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true' EXIT

sleep 2

kill "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true

if ! [ -f "$SERIAL_LOG" ]; then
  echo "missing serial log: $SERIAL_LOG" >&2
  exit 1
fi

if grep -q "stage1 ok" "$SERIAL_LOG"; then
  echo "stage1 serial test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "stage1 serial test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
