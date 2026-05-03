#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/invalid-opcode.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
source "$ROOT_DIR/scripts/qemu-test-lib.sh"

serial_log_has_expected_markers() {
  local serial_log="$1"

  grep -q "heap alloc ok" "$serial_log" \
    && grep -q "tss_rsp0=0x" "$serial_log" \
    && grep -q "tss_ist1=0x" "$serial_log" \
    && grep -q "tss_task_register=0x0000000000000028" "$serial_log" \
    && grep -q "tss_io_map_base=104" "$serial_log" \
    && grep -q "tss ok" "$serial_log" \
    && grep -q "address_space_kernel_root=0x" "$serial_log" \
    && grep -q "address_space_user_root=0x" "$serial_log" \
    && grep -q "address_space_user_base=0x0000000000400000" "$serial_log" \
    && grep -q "address_space_user_stack_top=0x0000000000800000" "$serial_log" \
    && grep -q "address_space_user_page_phys=0x" "$serial_log" \
    && grep -q "address_space_user_page_virt=0x0000000000400000" "$serial_log" \
    && grep -q "address_space_user_lookup=0x" "$serial_log" \
    && grep -q "address_space_mapped_user_pages=1" "$serial_log" \
    && grep -q "address_space ok" "$serial_log" \
    && grep -q "boot volume loaded ok" "$serial_log" \
    && grep -q "boot volume ok" "$serial_log" \
    && grep -q "file_layer ok" "$serial_log" \
    && grep -q "directory_layer ok" "$serial_log" \
    && grep -q "vfs_layer ok" "$serial_log" \
    && grep -q "fd_layer ok" "$serial_log" \
    && grep -q "syscall_layer ok" "$serial_log" \
    && grep -q "int80_syscall ok" "$serial_log" \
    && grep -q "user_mode_message=hello from ring3 via int80" "$serial_log" \
    && grep -q "user_mode_cwd=/" "$serial_log" \
    && grep -q "user_mode_readme_prefix=os64fs readme" "$serial_log" \
    && grep -q "user_mode_return_cs=0x0000000000000043" "$serial_log" \
    && grep -q "user_mode_return_cpl=3" "$serial_log" \
    && grep -q "user_mode_return_flags=0x0000000000000003" "$serial_log" \
    && grep -q "user mode ok" "$serial_log" \
    && grep -q "user_thread_pid=5" "$serial_log" \
    && grep -q "user_thread_tid=11" "$serial_log" \
    && grep -q "user_thread_kernel_cwd_before=/docs" "$serial_log" \
    && grep -q "user_thread_process_cwd_before=/" "$serial_log" \
    && grep -q "user_thread_root=0x" "$serial_log" \
    && grep -q "user_thread_code_phys=0x" "$serial_log" \
    && grep -q "user_thread_stack_phys=0x" "$serial_log" \
    && grep -q "user_thread_preempt_shared_phys=0x" "$serial_log" \
    && grep -q "user_thread_entry=0x0000000000400000" "$serial_log" \
    && grep -q "user_thread_preempt_shared_virt=0x0000000000401000" "$serial_log" \
    && grep -q "user_thread_stack_top=0x0000000000800000" "$serial_log" \
    && grep -q "user_thread_helper_tid=12" "$serial_log" \
    && grep -q "user_thread_kernel_entry_stack_top=0x" "$serial_log" \
    && grep -q "user_thread_program_size=" "$serial_log" \
    && grep -q "user_mode_yield_before=1" "$serial_log" \
    && grep -q "user_mode_yield_after=1" "$serial_log" \
    && grep -q "user_mode_preempt_before=1" "$serial_log" \
    && grep -q "user_mode_preempt_after=1" "$serial_log" \
    && grep -q "user_mode_stdin_before=1" "$serial_log" \
    && grep -q "user_mode_stdin_after=1" "$serial_log" \
    && grep -q "user_thread_return_cs=0x0000000000000043" "$serial_log" \
    && grep -q "user_thread_return_cpl=3" "$serial_log" \
    && grep -q "user_thread_return_flags=0x000000000000001F" "$serial_log" \
    && grep -q "user_thread_kernel_cwd_after=/docs" "$serial_log" \
    && grep -q "user_thread_process_cwd_after=/" "$serial_log" \
    && grep -q "user_thread_open_count=0" "$serial_log" \
    && grep -q "user_thread_tss_rsp0=0x" "$serial_log" \
    && grep -q "user_thread_kernel_root=0x" "$serial_log" \
    && grep -q "user_thread_helper_runs=" "$serial_log" \
    && grep -q "user_thread_helper_stack_base=0x" "$serial_log" \
    && grep -q "user_thread_helper_resume_root=0x" "$serial_log" \
    && grep -q "user_thread_helper_preempt_signals=1" "$serial_log" \
    && grep -q "user_thread_yield_count=1" "$serial_log" \
    && grep -q "user_thread_total_yield_count=" "$serial_log" \
    && grep -q "user_thread_preempt_count=" "$serial_log" \
    && grep -q "user_thread_preempt_arm_flag=1" "$serial_log" \
    && grep -q "user_thread_preempt_done_flag=1" "$serial_log" \
    && grep -q "user_thread_stdin_arm_flag=1" "$serial_log" \
    && grep -q "user_thread_stdin_done_flag=1" "$serial_log" \
    && grep -q "user_thread_helper_stdin_signals=1" "$serial_log" \
    && grep -q "user_thread_helper_block_observed=1" "$serial_log" \
    && grep -q "user_thread_trap_cs=0x0000000000000043" "$serial_log" \
    && grep -q "user_thread_trap_ss=0x000000000000003B" "$serial_log" \
    && grep -q "user_thread_trap_rip=0x000000000040" "$serial_log" \
    && grep -q "user_thread_trap_rsp=0x0000000000800000" "$serial_log" \
    && grep -q "user_thread_preempt_trap_vector=0x0000000000000020" "$serial_log" \
    && grep -q "user_thread_preempt_trap_cs=0x0000000000000043" "$serial_log" \
    && grep -q "user_thread_preempt_trap_ss=0x000000000000003B" "$serial_log" \
    && grep -q "user_thread_preempt_trap_rip=0x000000000040" "$serial_log" \
    && grep -q "user_thread_preempt_trap_rsp=0x0000000000" "$serial_log" \
    && grep -q "user_thread_process_state=exited" "$serial_log" \
    && grep -q "user_thread_state=finished" "$serial_log" \
    && grep -q "user thread ok" "$serial_log" \
    && grep -q "stdin_syscall ok" "$serial_log" \
    && grep -q "filesystem ok" "$serial_log" \
    && grep -q "scheduler ok" "$serial_log" \
    && grep -q "keyboard ok" "$serial_log" \
    && grep -q "console input ok" "$serial_log" \
    && grep -q "shell ok" "$serial_log" \
    && grep -q "invalid opcode smoke" "$serial_log" \
    && grep -q "invalid_opcode_marker=0x55443231494E5641" "$serial_log" \
    && grep -q "exception=invalid opcode" "$serial_log" \
    && grep -q "vector=0x0000000000000006" "$serial_log" \
    && grep -q "error_code=0x0000000000000000" "$serial_log" \
    && grep -q "fault_rip=0x" "$serial_log"
}

# 这一轮用 `ud2` 验证“通用 trap 框架”不是只会处理 page fault，
# 而是另一种完全不同的 CPU 异常也能从同一套 IDT/stub/handler 流进来。
KERNEL_EXTRA_CXXFLAGS="-DOS64_ENABLE_INVALID_OPCODE_SMOKE=1" \
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

# 这里不再固定等 3 秒，因为测试内核一旦继续变大，固定秒数就会变成假失败源。
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
  echo "invalid-opcode smoke test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "invalid-opcode smoke test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
