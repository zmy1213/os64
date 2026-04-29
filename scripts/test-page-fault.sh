#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/page-fault.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"

# 这一轮专门构建一个“故意触发 page fault”的测试镜像，
# 用来验证 IDT + 异常 stub + page fault 日志是否真的打通了。
KERNEL_EXTRA_CXXFLAGS="-DOS64_ENABLE_PAGE_FAULT_SMOKE=1" \
  bash "$ROOT_DIR/scripts/build-stage1-image.sh"

rm -f "$SERIAL_LOG"

"$QEMU_BIN" \
  -drive format=raw,file="$DISK_IMG",if=floppy,index=0 \
  -display none \
  -monitor none \
  -serial "file:$SERIAL_LOG" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
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

if grep -q "heap alloc ok" "$SERIAL_LOG" \
  && grep -q "keyboard ok" "$SERIAL_LOG" \
  && grep -q "console input ok" "$SERIAL_LOG" \
  && grep -q "page fault smoke" "$SERIAL_LOG" \
  && grep -q "page_fault_smoke_address=0x0000000000900000" "$SERIAL_LOG" \
  && grep -q "exception=page fault" "$SERIAL_LOG" \
  && grep -q "vector=0x000000000000000E" "$SERIAL_LOG" \
  && grep -q "fault_address=0x0000000000900000" "$SERIAL_LOG" \
  && grep -q "fault_rip=0x" "$SERIAL_LOG"; then
  echo "page-fault smoke test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "page-fault smoke test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
