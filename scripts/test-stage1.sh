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
  -drive format=raw,file="$DISK_IMG",if=floppy,index=0 \
  -display none \
  -monitor none \
  -serial "file:$SERIAL_LOG" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -no-reboot \
  -no-shutdown &

qemu_pid="$!"
trap 'kill "$qemu_pid" 2>/dev/null || true; wait "$qemu_pid" 2>/dev/null || true' EXIT

# Give BIOS and the two boot stages enough time to print their milestones.
sleep 2

# 如果来得及走到 bootloader/kernel 的 debug-exit 路径，QEMU 会自己退出；
# 如果没有退出，这里再兜底强制停止，避免测试卡死。
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
  && grep -q "idt ok" "$SERIAL_LOG" \
  && grep -q "boot info ok" "$SERIAL_LOG" \
  && grep -q "e820 parse ok" "$SERIAL_LOG" \
  && grep -q "page allocator ok" "$SERIAL_LOG" \
  && grep -q "alloc_page_0=0x" "$SERIAL_LOG" \
  && grep -q "alloc_page_1=0x" "$SERIAL_LOG" \
  && grep -q "alloc_page_2=0x" "$SERIAL_LOG" \
  && grep -q "mapped_test_page=0x" "$SERIAL_LOG" \
  && grep -q "mapped_virtual=0x" "$SERIAL_LOG" \
  && grep -q "read_back_value=0x1122334455667788" "$SERIAL_LOG" \
  && grep -q "map_page ok" "$SERIAL_LOG" \
  && grep -q "heap init ok" "$SERIAL_LOG" \
  && grep -q "heap_small=0x" "$SERIAL_LOG" \
  && grep -q "heap_large=0x" "$SERIAL_LOG" \
  && grep -q "heap_large_page_aligned=1" "$SERIAL_LOG" \
  && grep -q "heap_cross_page_value=0xFEDCBA9876543210" "$SERIAL_LOG" \
  && grep -q "heap_reuse=0x" "$SERIAL_LOG" \
  && grep -q "heap_coalesced=0x" "$SERIAL_LOG" \
  && grep -q "heap_free_bytes=" "$SERIAL_LOG" \
  && grep -q "heap alloc ok" "$SERIAL_LOG" \
  && grep -q "pic ok" "$SERIAL_LOG" \
  && grep -q "pit ok" "$SERIAL_LOG" \
  && grep -q "pit_frequency_hz=100" "$SERIAL_LOG" \
  && grep -q "timer_tick=10" "$SERIAL_LOG" \
  && grep -q "timer_tick=20" "$SERIAL_LOG" \
  && grep -q "timer_wait_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer_sleep_ms=50" "$SERIAL_LOG" \
  && grep -q "timer_sleep_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer ok" "$SERIAL_LOG"; then
  echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->pic->pit->timer->sleep serial test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->pic->pit->timer->sleep serial test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
