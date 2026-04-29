#!/usr/bin/env bash
set -euo pipefail

# Resolve paths once so the test can be run from any current directory.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/stage1-stage2.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"

# Always start from a fresh serial log so old output cannot mask failures.
rm -f "$SERIAL_LOG"

# Run QEMU headless and capture COM1 output into a file for assertions.
"$QEMU_BIN" \
  -drive format=raw,file="$DISK_IMG" \
  -display none \
  -monitor none \
  -serial "file:$SERIAL_LOG" \
  -no-reboot \
  -no-shutdown &

qemu_pid="$!"
trap 'kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true' EXIT

# Give BIOS and the two boot stages enough time to print their milestones.
sleep 2

# The test does not depend on QEMU debug-exit; it force-stops QEMU after capture.
kill "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true

# A missing log means the serial device was never set up correctly.
if ! [ -f "$SERIAL_LOG" ]; then
  echo "missing serial log: $SERIAL_LOG" >&2
  exit 1
fi

# Success now means we not only entered long mode, but also jumped into the first real C++ kernel.
if grep -q "stage1 ok" "$SERIAL_LOG" \
  && grep -q "stage2 ok" "$SERIAL_LOG" \
  && grep -q "a20 ok" "$SERIAL_LOG" \
  && grep -q "e820 ok" "$SERIAL_LOG" \
  && grep -q "kernel loaded ok" "$SERIAL_LOG" \
  && grep -q "protected mode ok" "$SERIAL_LOG" \
  && grep -q "paging ok" "$SERIAL_LOG" \
  && grep -q "long mode ok" "$SERIAL_LOG" \
  && grep -q "hello from os64 kernel" "$SERIAL_LOG" \
  && grep -q "boot info ok" "$SERIAL_LOG"; then
  echo "stage1->stage2->protected-mode->long-mode->kernel serial test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "stage1->stage2->protected-mode->long-mode->kernel serial test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
