#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/page-fault.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
source "$ROOT_DIR/scripts/qemu-test-lib.sh"

serial_log_has_expected_markers() {
  local serial_log="$1"

  grep -q "heap alloc ok" "$serial_log" \
    && grep -q "boot volume loaded ok" "$serial_log" \
    && grep -q "boot volume ok" "$serial_log" \
    && grep -q "file_layer ok" "$serial_log" \
    && grep -q "directory_layer ok" "$serial_log" \
    && grep -q "vfs_layer ok" "$serial_log" \
    && grep -q "fd_layer ok" "$serial_log" \
    && grep -q "syscall_layer ok" "$serial_log" \
    && grep -q "int80_syscall ok" "$serial_log" \
    && grep -q "filesystem ok" "$serial_log" \
    && grep -q "keyboard ok" "$serial_log" \
    && grep -q "console input ok" "$serial_log" \
    && grep -q "shell ok" "$serial_log" \
    && grep -q "page fault smoke" "$serial_log" \
    && grep -q "page_fault_smoke_address=0x0000000000900000" "$serial_log" \
    && grep -q "exception=page fault" "$serial_log" \
    && grep -q "vector=0x000000000000000E" "$serial_log" \
    && grep -q "fault_address=0x0000000000900000" "$serial_log" \
    && grep -q "fault_rip=0x" "$serial_log"
}

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

# 这里和 invalid-opcode 一样，改成“在上限时间里看串口里程碑”，避免镜像稍微变大就误杀 QEMU。
test_passed=0
if wait_for_serial_markers "$SERIAL_LOG" "$qemu_pid" 80 serial_log_has_expected_markers; then
  test_passed=1
fi

stop_qemu_if_running "$qemu_pid"

if ! [ -f "$SERIAL_LOG" ]; then
  echo "missing serial log: $SERIAL_LOG" >&2
  exit 1
fi

if [ "$test_passed" -eq 1 ]; then
  echo "page-fault smoke test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "page-fault smoke test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
