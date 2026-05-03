#!/usr/bin/env bash
set -euo pipefail

# Resolve paths once so the test can be run from any current directory.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DISK_IMG="$BUILD_DIR/disk.img"
SERIAL_LOG="$BUILD_DIR/stage1-stage2.serial.log"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
source "$ROOT_DIR/scripts/qemu-test-lib.sh"

serial_log_is_ready_to_stop() {
  local serial_log="$1"

  # `shell ok` 说明整条正常启动自测链已经走完。
  # 这里轮询时只看一个足够靠后的里程碑，最后真正判成功时仍然会跑下面那整套全量断言。
  grep -q "shell ok" "$serial_log"
}

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

# 现在不再固定 `sleep 2`。
# 原因是随着内核不断变大，“2 秒一定够”这个假设会越来越脆弱。
if ! wait_for_serial_markers "$SERIAL_LOG" "$qemu_pid" 80 serial_log_is_ready_to_stop; then
  :
fi

# 正常启动测试本身不会让 kernel_main 返回，所以拿到足够日志后主动收尾。
stop_qemu_if_running "$qemu_pid"

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
  && grep -q "boot volume loaded ok" "$SERIAL_LOG" \
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
  && grep -q "heap_active_allocations=" "$SERIAL_LOG" \
  && grep -q "heap_total_allocations=" "$SERIAL_LOG" \
  && grep -q "heap_failed_allocations=0" "$SERIAL_LOG" \
  && grep -q "heap alloc ok" "$SERIAL_LOG" \
  && grep -q "kernel_memory_page_allocator=0x" "$SERIAL_LOG" \
  && grep -q "kernel_memory_heap=0x" "$SERIAL_LOG" \
  && grep -q "kmalloc_block=0x" "$SERIAL_LOG" \
  && grep -q "kcalloc_block=0x" "$SERIAL_LOG" \
  && grep -q "kcalloc_zero_words=4" "$SERIAL_LOG" \
  && grep -q "kobject_sum=0x2222222222222222" "$SERIAL_LOG" \
  && grep -q "kobject_aligned_ptr=0x" "$SERIAL_LOG" \
  && grep -q "kobject_alignment=64" "$SERIAL_LOG" \
  && grep -q "kobject_ctor_count=2" "$SERIAL_LOG" \
  && grep -q "kobject_dtor_count=2" "$SERIAL_LOG" \
  && grep -q "kernel_memory_active_before=" "$SERIAL_LOG" \
  && grep -q "kernel_memory_active_after=" "$SERIAL_LOG" \
  && grep -q "kernel memory ok" "$SERIAL_LOG" \
  && grep -q "boot_volume_ptr=0x" "$SERIAL_LOG" \
  && grep -q "boot_volume_start_lba=" "$SERIAL_LOG" \
  && grep -q "boot_volume_sector_count=4" "$SERIAL_LOG" \
  && grep -q "boot_volume_sector_size=512" "$SERIAL_LOG" \
  && grep -q "block_device_total_bytes=2048" "$SERIAL_LOG" \
  && grep -q "block_device_sector0_prefix=0x" "$SERIAL_LOG" \
  && grep -q "boot volume ok" "$SERIAL_LOG" \
  && grep -q "os64fs_signature=OS64FSV1" "$SERIAL_LOG" \
  && grep -q "os64fs_volume_name=os64-root" "$SERIAL_LOG" \
  && grep -q "os64fs_inode_count=6" "$SERIAL_LOG" \
  && grep -q "os64fs_data_block_size=128" "$SERIAL_LOG" \
  && grep -q "os64fs_root_entries=3" "$SERIAL_LOG" \
  && grep -q "os64fs_docs_entries=1" "$SERIAL_LOG" \
  && grep -q "os64fs_parent_lookup_inode=3" "$SERIAL_LOG" \
  && grep -q "os64fs_readme=os64fs readme: the 64-bit kernel now mounts a real read-only filesystem." "$SERIAL_LOG" \
  && grep -q "os64fs_guide=os64fs guide: stage2 only preloads raw sectors." "$SERIAL_LOG" \
  && grep -q "file_open ok" "$SERIAL_LOG" \
  && grep -q "file_read_total=72" "$SERIAL_LOG" \
  && grep -q "file_eof_read=0" "$SERIAL_LOG" \
  && grep -q "file_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "file_layer ok" "$SERIAL_LOG" \
  && grep -q "directory_open ok" "$SERIAL_LOG" \
  && grep -q "directory_root_entries=3" "$SERIAL_LOG" \
  && grep -q "directory_read_count=3" "$SERIAL_LOG" \
  && grep -q "directory_rewind_index=0" "$SERIAL_LOG" \
  && grep -q "directory_docs_first_inode=5" "$SERIAL_LOG" \
  && grep -q "directory_layer ok" "$SERIAL_LOG" \
  && grep -q "vfs_mount ok" "$SERIAL_LOG" \
  && grep -q "vfs_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "vfs_file_read_total=72" "$SERIAL_LOG" \
  && grep -q "vfs_directory_entries=3" "$SERIAL_LOG" \
  && grep -q "vfs_directory_first_inode=2" "$SERIAL_LOG" \
  && grep -q "vfs_layer ok" "$SERIAL_LOG" \
  && grep -q "fd_table ok" "$SERIAL_LOG" \
  && grep -q "fd_open=0" "$SERIAL_LOG" \
  && grep -q "fd_read_total=72" "$SERIAL_LOG" \
  && grep -q "fd_eof_read=0" "$SERIAL_LOG" \
  && grep -q "fd_open_count=0" "$SERIAL_LOG" \
  && grep -q "fd_layer ok" "$SERIAL_LOG" \
  && grep -q "syscall_context ok" "$SERIAL_LOG" \
  && grep -q "sys_cwd=/" "$SERIAL_LOG" \
  && grep -q "sys_root_entries=3" "$SERIAL_LOG" \
  && grep -q "sys_cwd_after_cd=/docs" "$SERIAL_LOG" \
  && grep -q "sys_listdir_count=1" "$SERIAL_LOG" \
  && grep -q "sys_path_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "sys_write_stdout_payload=hello sys_write" "$SERIAL_LOG" \
  && grep -q "sys_write_stderr_payload=error sys_write" "$SERIAL_LOG" \
  && grep -q "sys_open=3" "$SERIAL_LOG" \
  && grep -q "sys_write_stdout_bytes=16" "$SERIAL_LOG" \
  && grep -q "sys_write_stderr_bytes=16" "$SERIAL_LOG" \
  && grep -q "sys_write_file_status=-6" "$SERIAL_LOG" \
  && grep -q "sys_write_bad_fd=-4" "$SERIAL_LOG" \
  && grep -q "sys_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "sys_read_total=193" "$SERIAL_LOG" \
  && grep -q "sys_eof_read=0" "$SERIAL_LOG" \
  && grep -q "sys_open_count=0" "$SERIAL_LOG" \
  && grep -q "syscall_layer ok" "$SERIAL_LOG" \
  && grep -q "int80_cwd=/" "$SERIAL_LOG" \
  && grep -q "int80_cwd_after_cd=/docs" "$SERIAL_LOG" \
  && grep -q "int80_listdir_count=1" "$SERIAL_LOG" \
  && grep -q "int80_path_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "int80_write_stdout_payload=hello int80_write" "$SERIAL_LOG" \
  && grep -q "int80_write_stderr_payload=error int80_write" "$SERIAL_LOG" \
  && grep -q "int80_open=3" "$SERIAL_LOG" \
  && grep -q "int80_fd_stat_inode=5" "$SERIAL_LOG" \
  && grep -q "int80_write_stdout_bytes=18" "$SERIAL_LOG" \
  && grep -q "int80_write_stderr_bytes=18" "$SERIAL_LOG" \
  && grep -q "int80_write_file_status=-6" "$SERIAL_LOG" \
  && grep -q "int80_write_bad_fd=-4" "$SERIAL_LOG" \
  && grep -q "int80_read_total=193" "$SERIAL_LOG" \
  && grep -q "int80_eof_read=0" "$SERIAL_LOG" \
  && grep -q "int80_open_count=0" "$SERIAL_LOG" \
  && grep -q "int80_bad_result=0xFFFFFFFFFFFFFFFF" "$SERIAL_LOG" \
  && grep -q "int80_syscall ok" "$SERIAL_LOG" \
  && grep -q "filesystem ok" "$SERIAL_LOG" \
  && grep -q "pic ok" "$SERIAL_LOG" \
  && grep -q "pit ok" "$SERIAL_LOG" \
  && grep -q "pit_frequency_hz=100" "$SERIAL_LOG" \
  && grep -q "timer_tick=10" "$SERIAL_LOG" \
  && grep -q "timer_tick=20" "$SERIAL_LOG" \
  && grep -q "timer_wait_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer_sleep_ms=50" "$SERIAL_LOG" \
  && grep -q "timer_sleep_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer ok" "$SERIAL_LOG" \
  && grep -q "sched_pid=1" "$SERIAL_LOG" \
  && grep -q "sched_thread_a_tid=1" "$SERIAL_LOG" \
  && grep -q "sched_thread_b_tid=2" "$SERIAL_LOG" \
  && grep -q "sched_trace=ABAB" "$SERIAL_LOG" \
  && grep -q "sched_process_state=exited" "$SERIAL_LOG" \
  && grep -q "sched_thread_a_state=finished" "$SERIAL_LOG" \
  && grep -q "sched_thread_b_state=finished" "$SERIAL_LOG" \
  && grep -q "sched_thread_a_ticks=" "$SERIAL_LOG" \
  && grep -q "sched_thread_b_ticks=" "$SERIAL_LOG" \
  && grep -q "sched_total_ticks=" "$SERIAL_LOG" \
  && grep -q "sched_total_switches=" "$SERIAL_LOG" \
  && grep -q "sched_total_yields=" "$SERIAL_LOG" \
  && grep -q "sched_preempt_requests=" "$SERIAL_LOG" \
  && grep -q "sched_ready_after=0" "$SERIAL_LOG" \
  && grep -q "sched_live_after=0" "$SERIAL_LOG" \
  && grep -q "scheduler ok" "$SERIAL_LOG" \
  && grep -q "keyboard init ok" "$SERIAL_LOG" \
  && grep -q "keyboard irq1 enabled" "$SERIAL_LOG" \
  && grep -q "keyboard_irq_count=7" "$SERIAL_LOG" \
  && grep -q "keyboard_last_scancode=0x000000000000000E" "$SERIAL_LOG" \
  && grep -q "keyboard_char_count=6" "$SERIAL_LOG" \
  && grep -q "keyboard_char_0=0x0000000000000061" "$SERIAL_LOG" \
  && grep -q "keyboard_char_1=0x0000000000000062" "$SERIAL_LOG" \
  && grep -q "keyboard_char_2=0x0000000000000031" "$SERIAL_LOG" \
  && grep -q "keyboard_char_3=0x0000000000000020" "$SERIAL_LOG" \
  && grep -q "keyboard_char_4=0x000000000000000A" "$SERIAL_LOG" \
  && grep -q "keyboard_char_5=0x0000000000000008" "$SERIAL_LOG" \
  && grep -q "keyboard_buffer_remaining=0" "$SERIAL_LOG" \
  && grep -q "keyboard_dropped_chars=0" "$SERIAL_LOG" \
  && grep -q "stdin_sys_read=3" "$SERIAL_LOG" \
  && grep -q "stdin_sys_empty=0" "$SERIAL_LOG" \
  && grep -q "stdin_int80_read=3" "$SERIAL_LOG" \
  && grep -q "stdin_int80_empty=0" "$SERIAL_LOG" \
  && grep -q "stdin_buffer_remaining=0" "$SERIAL_LOG" \
  && grep -q "stdin_dropped_chars=0" "$SERIAL_LOG" \
  && grep -q "stdin_syscall ok" "$SERIAL_LOG" \
  && grep -q "keyboard ok" "$SERIAL_LOG" \
  && grep -q "console_line_length=5" "$SERIAL_LOG" \
  && grep -q "console_line=os 64" "$SERIAL_LOG" \
  && grep -q "console_buffer_remaining=0" "$SERIAL_LOG" \
  && grep -q "console input ok" "$SERIAL_LOG" \
  && grep -q "shell_line=help" "$SERIAL_LOG" \
  && grep -q "shell_result=executed" "$SERIAL_LOG" \
  && grep -q "commands:" "$SERIAL_LOG" \
  && grep -q "help  - list commands" "$SERIAL_LOG" \
  && grep -q "mem   - show free physical pages" "$SERIAL_LOG" \
  && grep -q "ticks - show timer tick count" "$SERIAL_LOG" \
  && grep -q "heap  - show kernel heap stats" "$SERIAL_LOG" \
  && grep -q "disk  - show raw boot block device info" "$SERIAL_LOG" \
  && grep -q "pwd   - show current directory" "$SERIAL_LOG" \
  && grep -q "cd    - change current directory" "$SERIAL_LOG" \
  && grep -q "ls    - list directory entries" "$SERIAL_LOG" \
  && grep -q "cat   - print file contents" "$SERIAL_LOG" \
  && grep -q "stat  - show inode metadata" "$SERIAL_LOG" \
  && grep -q "irq   - show timer/keyboard irq stats" "$SERIAL_LOG" \
  && grep -q "bootinfo - show boot handoff info" "$SERIAL_LOG" \
  && grep -q "e820  - show boot memory map" "$SERIAL_LOG" \
  && grep -q "cpu   - show cpuid summary" "$SERIAL_LOG" \
  && grep -q "uptime - show tick-based uptime" "$SERIAL_LOG" \
  && grep -q "echo  - print text back" "$SERIAL_LOG" \
  && grep -q "history - show recent commands" "$SERIAL_LOG" \
  && grep -q "clear - clear console area" "$SERIAL_LOG" \
  && grep -q "shell_line=mem" "$SERIAL_LOG" \
  && grep -q "mem_free_pages=" "$SERIAL_LOG" \
  && grep -q "mem_free_bytes=" "$SERIAL_LOG" \
  && grep -q "shell_line=ticks" "$SERIAL_LOG" \
  && grep -q "ticks_current=" "$SERIAL_LOG" \
  && grep -q "shell_line=heap" "$SERIAL_LOG" \
  && grep -q "heap_used_bytes=" "$SERIAL_LOG" \
  && grep -q "heap_mapped_bytes=" "$SERIAL_LOG" \
  && grep -q "heap_free_bytes=" "$SERIAL_LOG" \
  && grep -q "heap_active_allocations=" "$SERIAL_LOG" \
  && grep -q "heap_total_allocations=" "$SERIAL_LOG" \
  && grep -q "heap_failed_allocations=0" "$SERIAL_LOG" \
  && grep -q "shell_line=disk" "$SERIAL_LOG" \
  && grep -q "disk_start_lba=" "$SERIAL_LOG" \
  && grep -q "disk_sector_count=4" "$SERIAL_LOG" \
  && grep -q "disk_sector_size=512" "$SERIAL_LOG" \
  && grep -q "disk_total_bytes=2048" "$SERIAL_LOG" \
  && grep -q "shell_line=ls" "$SERIAL_LOG" \
  && grep -q "ls_path=/" "$SERIAL_LOG" \
  && grep -q "ls_resolved_path=/" "$SERIAL_LOG" \
  && grep -q "ls_entry_count=3" "$SERIAL_LOG" \
  && grep -q "ls\\[0\\]=file readme.txt size=" "$SERIAL_LOG" \
  && grep -q "ls\\[1\\]=file notes.txt size=" "$SERIAL_LOG" \
  && grep -q "ls\\[2\\]=dir docs size=" "$SERIAL_LOG" \
  && grep -q "shell_line=ls docs" "$SERIAL_LOG" \
  && grep -q "ls_path=docs" "$SERIAL_LOG" \
  && grep -q "ls_resolved_path=/docs" "$SERIAL_LOG" \
  && grep -q "ls_entry_count=1" "$SERIAL_LOG" \
  && grep -q "ls\\[0\\]=file guide.txt size=" "$SERIAL_LOG" \
  && grep -q "shell_line=cat readme.txt" "$SERIAL_LOG" \
  && grep -q "cat_path=readme.txt" "$SERIAL_LOG" \
  && grep -q "cat_resolved_path=/readme.txt" "$SERIAL_LOG" \
  && grep -q "cat_size=" "$SERIAL_LOG" \
  && grep -q "os64fs readme: the 64-bit kernel now mounts a real read-only filesystem." "$SERIAL_LOG" \
  && grep -q "shell_line=cat /docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "cat_path=/docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "cat_resolved_path=/docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "os64fs guide: stage2 only preloads raw sectors." "$SERIAL_LOG" \
  && grep -q "shell_line=stat docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "stat_path=docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "stat_resolved_path=/docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "stat_inode=5" "$SERIAL_LOG" \
  && grep -q "stat_type=file" "$SERIAL_LOG" \
  && grep -q "stat_links=1" "$SERIAL_LOG" \
  && grep -q "stat_blocks=2" "$SERIAL_LOG" \
  && grep -q "stat_block_0=4" "$SERIAL_LOG" \
  && grep -q "stat_block_1=5" "$SERIAL_LOG" \
  && grep -q "shell_line=stat docs/../notes.txt" "$SERIAL_LOG" \
  && grep -q "stat_path=docs/../notes.txt" "$SERIAL_LOG" \
  && grep -q "stat_resolved_path=/notes.txt" "$SERIAL_LOG" \
  && grep -q "stat_inode=3" "$SERIAL_LOG" \
  && grep -q "stat_blocks=1" "$SERIAL_LOG" \
  && grep -q "stat_block_0=2" "$SERIAL_LOG" \
  && grep -q "shell_line=pwd" "$SERIAL_LOG" \
  && grep -q "pwd_path=/" "$SERIAL_LOG" \
  && grep -q "shell_line=cd docs" "$SERIAL_LOG" \
  && grep -q "cwd_path=/docs" "$SERIAL_LOG" \
  && grep -q "pwd_path=/docs" "$SERIAL_LOG" \
  && grep -q "ls_path=/docs" "$SERIAL_LOG" \
  && grep -q "cat_path=guide.txt" "$SERIAL_LOG" \
  && grep -q "cat_resolved_path=/docs/guide.txt" "$SERIAL_LOG" \
  && grep -q "shell_line=irq" "$SERIAL_LOG" \
  && grep -q "irq_timer_ticks=" "$SERIAL_LOG" \
  && grep -q "irq_timer_frequency_hz=100" "$SERIAL_LOG" \
  && grep -q "irq_keyboard_count=" "$SERIAL_LOG" \
  && grep -q "irq_keyboard_buffered_chars=0" "$SERIAL_LOG" \
  && grep -q "irq_keyboard_dropped_chars=0" "$SERIAL_LOG" \
  && grep -q "shell_line=bootinfo" "$SERIAL_LOG" \
  && grep -q "bootinfo_magic=0x" "$SERIAL_LOG" \
  && grep -q "bootinfo_memory_map_count=" "$SERIAL_LOG" \
  && grep -q "bootinfo_memory_map_entry_size=24" "$SERIAL_LOG" \
  && grep -q "bootinfo_memory_map_ptr=0x" "$SERIAL_LOG" \
  && grep -q "bootinfo_boot_volume_ptr=0x" "$SERIAL_LOG" \
  && grep -q "bootinfo_boot_volume_start_lba=" "$SERIAL_LOG" \
  && grep -q "bootinfo_boot_volume_sector_count=4" "$SERIAL_LOG" \
  && grep -q "bootinfo_boot_volume_sector_size=512" "$SERIAL_LOG" \
  && grep -q "shell_line=e820" "$SERIAL_LOG" \
  && grep -q "e820_count=" "$SERIAL_LOG" \
  && grep -Fq "e820_shell[0] base=0x" "$SERIAL_LOG" \
  && grep -q "shell_line=cpu" "$SERIAL_LOG" \
  && grep -q "cpu_vendor=" "$SERIAL_LOG" \
  && grep -q "cpu_max_basic_leaf=0x" "$SERIAL_LOG" \
  && grep -q "cpu_max_extended_leaf=0x" "$SERIAL_LOG" \
  && grep -q "cpu_long_mode=1" "$SERIAL_LOG" \
  && grep -q "shell_line=echo hi42" "$SERIAL_LOG" \
  && grep -q $'^hi42\r$' "$SERIAL_LOG" \
  && grep -q "shell_line=uptime" "$SERIAL_LOG" \
  && grep -q "uptime_ticks=" "$SERIAL_LOG" \
  && grep -q "uptime_frequency_hz=100" "$SERIAL_LOG" \
  && grep -q "uptime_ms=" "$SERIAL_LOG" \
  && grep -q "shell_line=history" "$SERIAL_LOG" \
  && grep -q "history_buffered=23" "$SERIAL_LOG" \
  && grep -q "history_total=23" "$SERIAL_LOG" \
  && grep -Fq "history[1]=help" "$SERIAL_LOG" \
  && grep -Fq "history[2]=mem" "$SERIAL_LOG" \
  && grep -Fq "history[3]=ticks" "$SERIAL_LOG" \
  && grep -Fq "history[4]=heap" "$SERIAL_LOG" \
  && grep -Fq "history[5]=disk" "$SERIAL_LOG" \
  && grep -Fq "history[6]=ls" "$SERIAL_LOG" \
  && grep -Fq "history[7]=ls docs" "$SERIAL_LOG" \
  && grep -Fq "history[8]=cat readme.txt" "$SERIAL_LOG" \
  && grep -Fq "history[9]=cat /docs/guide.txt" "$SERIAL_LOG" \
  && grep -Fq "history[10]=stat docs/guide.txt" "$SERIAL_LOG" \
  && grep -Fq "history[11]=stat docs/../notes.txt" "$SERIAL_LOG" \
  && grep -Fq "history[12]=pwd" "$SERIAL_LOG" \
  && grep -Fq "history[13]=cd docs" "$SERIAL_LOG" \
  && grep -Fq "history[14]=pwd" "$SERIAL_LOG" \
  && grep -Fq "history[15]=ls" "$SERIAL_LOG" \
  && grep -Fq "history[16]=cat guide.txt" "$SERIAL_LOG" \
  && grep -Fq "history[17]=irq" "$SERIAL_LOG" \
  && grep -Fq "history[18]=bootinfo" "$SERIAL_LOG" \
  && grep -Fq "history[19]=e820" "$SERIAL_LOG" \
  && grep -Fq "history[20]=cpu" "$SERIAL_LOG" \
  && grep -Fq "history[21]=echo hi42" "$SERIAL_LOG" \
  && grep -Fq "history[22]=uptime" "$SERIAL_LOG" \
  && grep -Fq "history[23]=history" "$SERIAL_LOG" \
  && grep -q "history_buffered=24" "$SERIAL_LOG" \
  && grep -q "history_total=24" "$SERIAL_LOG" \
  && grep -Fq "history[24]=history" "$SERIAL_LOG" \
  && grep -q "shell_line=clear" "$SERIAL_LOG" \
  && grep -q "shell_line=bad" "$SERIAL_LOG" \
  && grep -q "shell_result=unknown" "$SERIAL_LOG" \
  && grep -q "unknown command: bad" "$SERIAL_LOG" \
  && grep -q "shell ok" "$SERIAL_LOG"; then
  echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->kmemory->boot-volume->filesystem->file-layer->directory-layer->vfs->fd->syscall->pic->pit->timer->sleep->keyboard->char-input->console-line->shell serial test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->kmemory->boot-volume->filesystem->syscall->pic->pit->timer->sleep->keyboard->char-input->console-line->shell serial test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
