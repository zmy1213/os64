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
  && grep -q "boot_volume_signature=OS64VOL1" "$SERIAL_LOG" \
  && grep -q "boot_volume_name=boot-volume" "$SERIAL_LOG" \
  && grep -q "boot_volume_readme=boot volume sector 1: hello from os64" "$SERIAL_LOG" \
  && grep -q "boot_volume_notes=boot volume sector 2: next step is filesystem" "$SERIAL_LOG" \
  && grep -q "disk read ok" "$SERIAL_LOG" \
  && grep -q "pic ok" "$SERIAL_LOG" \
  && grep -q "pit ok" "$SERIAL_LOG" \
  && grep -q "pit_frequency_hz=100" "$SERIAL_LOG" \
  && grep -q "timer_tick=10" "$SERIAL_LOG" \
  && grep -q "timer_tick=20" "$SERIAL_LOG" \
  && grep -q "timer_wait_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer_sleep_ms=50" "$SERIAL_LOG" \
  && grep -q "timer_sleep_elapsed_ticks=" "$SERIAL_LOG" \
  && grep -q "timer ok" "$SERIAL_LOG" \
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
  && grep -q "disk  - show boot volume info" "$SERIAL_LOG" \
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
  && grep -q "disk_signature=OS64VOL1" "$SERIAL_LOG" \
  && grep -q "disk_volume_name=boot-volume" "$SERIAL_LOG" \
  && grep -q "disk_readme_sector=1" "$SERIAL_LOG" \
  && grep -q "disk_notes_sector=2" "$SERIAL_LOG" \
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
  && grep -q "history_buffered=12" "$SERIAL_LOG" \
  && grep -q "history_total=12" "$SERIAL_LOG" \
  && grep -Fq "history[1]=help" "$SERIAL_LOG" \
  && grep -Fq "history[2]=mem" "$SERIAL_LOG" \
  && grep -Fq "history[3]=ticks" "$SERIAL_LOG" \
  && grep -Fq "history[4]=heap" "$SERIAL_LOG" \
  && grep -Fq "history[5]=disk" "$SERIAL_LOG" \
  && grep -Fq "history[6]=irq" "$SERIAL_LOG" \
  && grep -Fq "history[7]=bootinfo" "$SERIAL_LOG" \
  && grep -Fq "history[8]=e820" "$SERIAL_LOG" \
  && grep -Fq "history[9]=cpu" "$SERIAL_LOG" \
  && grep -Fq "history[10]=echo hi42" "$SERIAL_LOG" \
  && grep -Fq "history[11]=uptime" "$SERIAL_LOG" \
  && grep -Fq "history[12]=history" "$SERIAL_LOG" \
  && grep -q "history_buffered=13" "$SERIAL_LOG" \
  && grep -q "history_total=13" "$SERIAL_LOG" \
  && grep -Fq "history[13]=history" "$SERIAL_LOG" \
  && grep -q "shell_line=clear" "$SERIAL_LOG" \
  && grep -q "shell_line=bad" "$SERIAL_LOG" \
  && grep -q "shell_result=unknown" "$SERIAL_LOG" \
  && grep -q "unknown command: bad" "$SERIAL_LOG" \
  && grep -q "shell ok" "$SERIAL_LOG"; then
  echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->kmemory->boot-volume->pic->pit->timer->sleep->keyboard->char-input->console-line->shell serial test passed"
  cat "$SERIAL_LOG"
  exit 0
fi

echo "stage1->stage2->protected-mode->long-mode->kernel->idt->allocator->paging->heap->kmemory->boot-volume->pic->pit->timer->sleep->keyboard->char-input->console-line->shell serial test failed" >&2
cat "$SERIAL_LOG" >&2
exit 1
