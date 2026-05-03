#include "shell/shell.hpp"

#include "console/console.hpp"
#include "fs/os64fs.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"
#include "storage/boot_volume.hpp"
#include "task/scheduler.hpp"

namespace {

constexpr size_t kShellListDirCapacity = 8;  // 当前教学文件系统很小，先用固定数组装目录项，保持实现简单可预测。
constexpr uint64_t kShellRunUserStackTop = 0x0000000000800000ULL;  // 先继续复用当前教学内核统一的用户栈顶。
constexpr uint64_t kShellRunDefaultUserRflags = 0x202ULL;          // bit1 恒为 1，并先把 IF 打开，让 shell 启动的用户程序也能继续接收外部 IRQ。

struct CpuidResult {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
};

void write_char(const ShellState* shell, char ch) {
  if (shell == nullptr || shell->output.write_char == nullptr) {
    return;
  }

  shell->output.write_char(ch);
}

void write_string(const ShellState* shell, const char* text) {
  if (text == nullptr) {
    return;
  }

  for (size_t i = 0; text[i] != '\0'; ++i) {
    write_char(shell, text[i]);
  }
}

void write_newline(const ShellState* shell) {
  write_char(shell, '\n');
}

void clear_output(const ShellState* shell) {
  if (shell == nullptr || shell->output.clear == nullptr) {
    return;
  }

  shell->output.clear();
}

void set_output_color(const ShellState* shell, uint8_t color) {
  if (shell == nullptr || shell->output.set_color == nullptr) {
    return;
  }

  shell->output.set_color(color);
}

void write_u64(const ShellState* shell, uint64_t value) {
  char digits[20];
  size_t count = 0;

  if (value == 0) {
    write_char(shell, '0');
    return;
  }

  while (value != 0 && count < sizeof(digits)) {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }

  while (count > 0) {
    write_char(shell, digits[--count]);
  }
}

void write_hex_nibble(const ShellState* shell, uint8_t value) {
  if (value < 10) {
    write_char(shell, static_cast<char>('0' + value));
    return;
  }

  write_char(shell, static_cast<char>('A' + (value - 10)));
}

void write_hex64(const ShellState* shell, uint64_t value) {
  for (int shift = 60; shift >= 0; shift -= 4) {
    const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0x0F);
    write_hex_nibble(shell, nibble);
  }
}

void write_bounded_string(const ShellState* shell,
                          const char* text,
                          size_t limit) {
  if (text == nullptr) {
    return;
  }

  for (size_t i = 0; i < limit && text[i] != '\0'; ++i) {
    write_char(shell, text[i]);
  }
}

bool is_space_char(char ch) {
  return ch == ' ' || ch == '\t';
}

const char* skip_spaces(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }

  while (is_space_char(*text)) {
    ++text;
  }

  return text;
}

bool is_empty_after_trim(const char* text) {
  const char* trimmed = skip_spaces(text);
  return trimmed == nullptr || trimmed[0] == '\0';
}

size_t string_length(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

const char* path_leaf_name(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }

  const char* leaf = path;
  for (size_t i = 0; path[i] != '\0'; ++i) {
    if (path[i] == '/') {
      leaf = path + i + 1;
    }
  }

  return (leaf[0] != '\0') ? leaf : path;
}

const char* trim_trailing_spaces(const char* begin, const char* end) {
  while (end > begin && is_space_char(end[-1])) {
    --end;
  }

  return end;
}

bool copy_text_slice(char* destination,
                     size_t capacity,
                     const char* begin,
                     const char* end) {
  if (destination == nullptr || begin == nullptr || end == nullptr ||
      end < begin || capacity == 0) {
    return false;
  }

  const size_t length = static_cast<size_t>(end - begin);
  if (length >= capacity) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    destination[i] = begin[i];
  }
  destination[length] = '\0';
  return true;
}

bool split_path_and_text(const char* arguments,
                         char* out_path,
                         size_t path_capacity,
                         const char** out_text) {
  if (arguments == nullptr || out_path == nullptr || path_capacity == 0 ||
      out_text == nullptr) {
    return false;
  }

  const char* path_begin = skip_spaces(arguments);
  if (path_begin == nullptr || path_begin[0] == '\0') {
    return false;
  }

  const char* path_end = path_begin;
  while (path_end[0] != '\0' && !is_space_char(path_end[0])) {
    ++path_end;
  }

  if (!copy_text_slice(out_path, path_capacity, path_begin, path_end)) {
    return false;
  }

  *out_text = skip_spaces(path_end);
  if (*out_text == nullptr) {
    *out_text = path_end;
  }
  return true;
}

bool is_boot_info_valid(const BootInfo* boot_info) {
  return boot_info != nullptr &&
         boot_info->magic == kBootInfoMagic &&
         boot_info->memory_map_ptr != 0 &&
         boot_info->memory_map_entry_size == sizeof(E820Entry);
}

const char* memory_kind_name(uint32_t type) {
  if (type == kE820TypeUsable) {
    return "usable";
  }

  return "reserved";
}

CpuidResult read_cpuid(uint32_t leaf, uint32_t subleaf) {
  CpuidResult result;
  result.eax = 0;
  result.ebx = 0;
  result.ecx = 0;
  result.edx = 0;
  asm volatile("cpuid"
               : "=a"(result.eax), "=b"(result.ebx),
                 "=c"(result.ecx), "=d"(result.edx)
               : "a"(leaf), "c"(subleaf));
  return result;
}

size_t history_slot_index(const ShellState* shell, size_t history_index) {
  if (shell == nullptr || history_index >= shell->history_count) {
    return 0;
  }

  if (shell->history_count < kShellHistoryCapacity) {
    return history_index;
  }

  return (shell->history_next_slot + history_index) % kShellHistoryCapacity;
}

size_t history_provider_entry_count(const void* context) {
  const auto* shell = static_cast<const ShellState*>(context);
  return (shell != nullptr) ? shell->history_count : 0;
}

const char* history_provider_entry_text(const void* context, size_t index) {
  const auto* shell = static_cast<const ShellState*>(context);
  if (shell == nullptr || index >= shell->history_count) {
    return nullptr;
  }

  return shell->history_entries[history_slot_index(shell, index)];
}

void record_history_line(ShellState* shell, const char* line) {
  if (shell == nullptr || line == nullptr) {
    return;
  }

  const char* trimmed_begin = skip_spaces(line);
  if (trimmed_begin == nullptr || trimmed_begin[0] == '\0') {
    return;
  }

  const char* trimmed_end = trimmed_begin + string_length(trimmed_begin);
  trimmed_end = trim_trailing_spaces(trimmed_begin, trimmed_end);
  if (trimmed_end <= trimmed_begin) {
    return;
  }

  const size_t slot = shell->history_next_slot;
  const size_t line_length =
      static_cast<size_t>(trimmed_end - trimmed_begin);
  const size_t copy_length =
      (line_length < (kShellHistoryEntryCapacity - 1))
          ? line_length
          : (kShellHistoryEntryCapacity - 1);

  // 历史记录先直接写进固定槽位。
  // 这一轮不走堆分配，而是故意用固定容量 ring buffer 保持行为可预测。
  for (size_t i = 0; i < copy_length; ++i) {
    shell->history_entries[slot][i] = trimmed_begin[i];
  }
  shell->history_entries[slot][copy_length] = '\0';

  shell->history_sequence_numbers[slot] = shell->history_total_count + 1;
  ++shell->history_total_count;

  if (shell->history_count < kShellHistoryCapacity) {
    ++shell->history_count;
  }

  shell->history_next_slot =
      static_cast<uint16_t>((slot + 1) % kShellHistoryCapacity);
}

// 这就是这一轮最关键的小升级：
// 以前 shell 只能看“整行是不是刚好等于 help/mem/ticks”；
// 现在它先识别命令名，再把后面的剩余部分当成参数区。
bool command_matches(const char* line,
                     const char* command,
                     const char** out_args) {
  if (line == nullptr || command == nullptr) {
    return false;
  }

  const char* cursor = skip_spaces(line);
  size_t index = 0;

  while (command[index] != '\0') {
    if (cursor[index] != command[index]) {
      return false;
    }

    ++index;
  }

  if (cursor[index] == '\0') {
    if (out_args != nullptr) {
      *out_args = cursor + index;
    }
    return true;
  }

  if (!is_space_char(cursor[index])) {
    return false;
  }

  if (out_args != nullptr) {
    *out_args = skip_spaces(cursor + index);
  }

  return true;
}

void handle_help_command(const ShellState* shell) {
  write_string(shell, "commands:");
  write_newline(shell);
  write_string(shell, "help  - list commands");
  write_newline(shell);
  write_string(shell, "mem   - show free physical pages");
  write_newline(shell);
  write_string(shell, "ticks - show timer tick count");
  write_newline(shell);
  write_string(shell, "heap  - show kernel heap stats");
  write_newline(shell);
  write_string(shell, "disk  - show raw boot block device info");
  write_newline(shell);
  write_string(shell, "pwd   - show current directory");
  write_newline(shell);
  write_string(shell, "cd    - change current directory");
  write_newline(shell);
  write_string(shell, "ls    - list directory entries");
  write_newline(shell);
  write_string(shell, "cat   - print file contents");
  write_newline(shell);
  write_string(shell, "stat  - show inode metadata");
  write_newline(shell);
  write_string(shell, "touch - create an empty file");
  write_newline(shell);
  write_string(shell, "mkdir - create a directory");
  write_newline(shell);
  write_string(shell, "write - replace a file with text");
  write_newline(shell);
  write_string(shell, "append - append text to a file");
  write_newline(shell);
  write_string(shell, "rm    - remove a file or empty dir");
  write_newline(shell);
  write_string(shell, "sync  - flush filesystem metadata");
  write_newline(shell);
  write_string(shell, "run   - load and start a user ELF from OS64FS");
  write_newline(shell);
  write_string(shell, "irq   - show timer/keyboard irq stats");
  write_newline(shell);
  write_string(shell, "bootinfo - show boot handoff info");
  write_newline(shell);
  write_string(shell, "e820  - show boot memory map");
  write_newline(shell);
  write_string(shell, "cpu   - show cpuid summary");
  write_newline(shell);
  write_string(shell, "uptime - show tick-based uptime");
  write_newline(shell);
  write_string(shell, "echo  - print text back");
  write_newline(shell);
  write_string(shell, "history - show recent commands");
  write_newline(shell);
  write_string(shell, "clear - clear console area");
  write_newline(shell);
}

void handle_mem_command(const ShellState* shell) {
  if (shell == nullptr || shell->allocator == nullptr) {
    write_string(shell, "mem unavailable");
    write_newline(shell);
    return;
  }

  const uint64_t free_pages = count_free_pages(shell->allocator);
  const uint64_t free_bytes = free_pages * kPageSize;

  write_string(shell, "mem_free_pages=");
  write_u64(shell, free_pages);
  write_newline(shell);

  write_string(shell, "mem_free_bytes=");
  write_u64(shell, free_bytes);
  write_newline(shell);
}

void handle_ticks_command(const ShellState* shell) {
  write_string(shell, "ticks_current=");
  write_u64(shell, timer_tick_count());
  write_newline(shell);
}

void handle_heap_command(const ShellState* shell) {
  if (shell == nullptr || shell->heap == nullptr) {
    write_string(shell, "heap unavailable");
    write_newline(shell);
    return;
  }

  write_string(shell, "heap_used_bytes=");
  write_u64(shell, heap_used_bytes(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_mapped_bytes=");
  write_u64(shell, heap_mapped_bytes(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_free_bytes=");
  write_u64(shell, heap_free_bytes(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_active_allocations=");
  write_u64(shell, heap_active_allocations(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_total_allocations=");
  write_u64(shell, heap_total_allocations(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_failed_allocations=");
  write_u64(shell, heap_failed_allocations(shell->heap));
  write_newline(shell);
}

void handle_disk_command(const ShellState* shell) {
  if (shell == nullptr || !block_device_is_ready(shell->block_device)) {
    write_string(shell, "disk unavailable");
    write_newline(shell);
    return;
  }
  write_string(shell, "disk_start_lba=");
  write_u64(shell, shell->block_device->start_lba);
  write_newline(shell);

  write_string(shell, "disk_sector_count=");
  write_u64(shell, shell->block_device->sector_count);
  write_newline(shell);

  write_string(shell, "disk_sector_size=");
  write_u64(shell, shell->block_device->sector_size);
  write_newline(shell);

  write_string(shell, "disk_total_bytes=");
  write_u64(shell, block_device_total_bytes(shell->block_device));
  write_newline(shell);

  if (shell->filesystem == nullptr || !os64fs_is_mounted(shell->filesystem)) {
    return;
  }

  const Os64FsSuperblock* const superblock =
      os64fs_superblock(shell->filesystem);
  Os64FsStats stats;
  if (superblock == nullptr ||
      !os64fs_query_stats(shell->filesystem, &stats)) {
    return;
  }

  write_string(shell, "disk_fs_version=");
  write_u64(shell, superblock->version);
  write_newline(shell);

  write_string(shell, "disk_inode_bitmap_sectors=");
  write_u64(shell, superblock->inode_bitmap_sector_count);
  write_newline(shell);

  write_string(shell, "disk_data_bitmap_sectors=");
  write_u64(shell, superblock->data_bitmap_sector_count);
  write_newline(shell);

  write_string(shell, "disk_inode_total=");
  write_u64(shell, stats.allocatable_inodes);
  write_newline(shell);

  write_string(shell, "disk_inode_used=");
  write_u64(shell, stats.used_inodes);
  write_newline(shell);

  write_string(shell, "disk_inode_free=");
  write_u64(shell, stats.free_inodes);
  write_newline(shell);

  write_string(shell, "disk_data_blocks=");
  write_u64(shell, stats.total_data_blocks);
  write_newline(shell);

  write_string(shell, "disk_data_used=");
  write_u64(shell, stats.used_data_blocks);
  write_newline(shell);

  write_string(shell, "disk_data_free=");
  write_u64(shell, stats.free_data_blocks);
  write_newline(shell);
}

void handle_pwd_command(const ShellState* shell) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    return;
  }

  char cwd[kSyscallPathCapacity];
  if (sys_getcwd(shell->syscall_context, cwd, sizeof(cwd)) < 0) {
    write_string(shell, "pwd unavailable");
    write_newline(shell);
    return;
  }

  write_string(shell, "pwd_path=");
  write_string(shell, cwd);
  write_newline(shell);
}

void handle_cd_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    path = "/";
  }

  char cwd[kSyscallPathCapacity];
  const SyscallStatus status =
      sys_chdir(shell->syscall_context, path);
  if (status == kSyscallNotFound) {
    write_string(shell, "cd path not found: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (status == kSyscallNotFile) {
    write_string(shell, "cd not a directory: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (status != kSyscallOk ||
      sys_getcwd(shell->syscall_context, cwd, sizeof(cwd)) < 0) {
    write_string(shell, "cd path too long");
    write_newline(shell);
    return;
  }

  write_string(shell, "cwd_path=");
  write_string(shell, cwd);
  write_newline(shell);
}

void handle_ls_command(const ShellState* shell, const char* arguments) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  char implicit_path[kSyscallPathCapacity];
  if (path == nullptr || path[0] == '\0') {
    if (sys_getcwd(shell->syscall_context, implicit_path,
                   sizeof(implicit_path)) < 0) {
      write_string(shell, "ls unavailable");
      write_newline(shell);
      return;
    }
    path = implicit_path;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "ls path too long");
    write_newline(shell);
    return;
  }

  VfsStat stat;
  if (sys_stat_path(shell->syscall_context, path, &stat) != kSyscallOk) {
    write_string(shell, "ls path not found: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (stat.type != kVfsNodeTypeDirectory) {
    write_string(shell, "ls not a directory: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  const int32_t entry_count =
      sys_listdir(shell->syscall_context, path, nullptr, 0);
  if (entry_count < 0 ||
      static_cast<size_t>(entry_count) > kShellListDirCapacity) {
    write_string(shell, "ls open failed");
    write_newline(shell);
    return;
  }

  VfsDirectoryEntry entries[kShellListDirCapacity];
  const int32_t copied_count =
      sys_listdir(shell->syscall_context, path, entries,
                  static_cast<size_t>(entry_count));
  if (copied_count != entry_count) {
    write_string(shell, "ls read failed");
    write_newline(shell);
    return;
  }

  write_string(shell, "ls_path=");
  write_string(shell, path);
  write_newline(shell);

  write_string(shell, "ls_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);

  write_string(shell, "ls_entry_count=");
  write_u64(shell, static_cast<uint64_t>(entry_count));
  write_newline(shell);

  for (int32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
    const VfsDirectoryEntry& entry =
        entries[static_cast<size_t>(entry_index)];
    write_string(shell, "ls[");
    write_u64(shell, static_cast<uint64_t>(entry_index));
    write_string(shell, "]=");
    write_string(shell, vfs_node_type_name(entry.type));
    write_char(shell, ' ');
    write_bounded_string(shell, entry.name, entry.name_length);
    write_string(shell, " size=");
    write_u64(shell, entry.size_bytes);
    write_newline(shell);
  }
}

void handle_cat_command(const ShellState* shell, const char* arguments) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: cat <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "cat path too long");
    write_newline(shell);
    return;
  }

  const int32_t fd = sys_open(shell->syscall_context, path);
  if (fd == kSyscallNotFound) {
    write_string(shell, "cat path not found: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (fd == kSyscallNotFile) {
    write_string(shell, "cat not a file: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (fd < 0) {
    write_string(shell, "cat open failed");
    write_newline(shell);
    return;
  }

  VfsStat fd_stat_result;
  if (sys_stat(shell->syscall_context, fd, &fd_stat_result) != kSyscallOk) {
    write_string(shell, "cat stat failed");
    write_newline(shell);
    (void)sys_close(shell->syscall_context, fd);
    return;
  }

  write_string(shell, "cat_path=");
  write_string(shell, path);
  write_newline(shell);

  write_string(shell, "cat_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);

  write_string(shell, "cat_size=");
  write_u64(shell, fd_stat_result.size_bytes);
  write_newline(shell);

  uint8_t chunk[64];
  uint32_t total_read = 0;
  while (total_read < fd_stat_result.size_bytes) {
    const int32_t bytes_this_round =
        sys_read(shell->syscall_context, fd, chunk, sizeof(chunk));
    if (bytes_this_round <= 0) {
      write_string(shell, "cat read failed");
      write_newline(shell);
      (void)sys_close(shell->syscall_context, fd);
      return;
    }

    total_read += static_cast<uint32_t>(bytes_this_round);
    for (int32_t i = 0; i < bytes_this_round; ++i) {
      write_char(shell, static_cast<char>(chunk[i]));
    }
  }

  (void)sys_close(shell->syscall_context, fd);
  write_newline(shell);
}

void handle_stat_command(const ShellState* shell, const char* arguments) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: stat <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "stat path too long");
    write_newline(shell);
    return;
  }

  VfsStat stat;
  if (sys_stat_path(shell->syscall_context, path, &stat) != kSyscallOk) {
    write_string(shell, "stat path not found: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "stat_path=");
  write_string(shell, path);
  write_newline(shell);

  write_string(shell, "stat_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);

  write_string(shell, "stat_inode=");
  write_u64(shell, stat.inode_number);
  write_newline(shell);

  write_string(shell, "stat_type=");
  write_string(shell, vfs_node_type_name(stat.type));
  write_newline(shell);

  write_string(shell, "stat_size=");
  write_u64(shell, stat.size_bytes);
  write_newline(shell);

  write_string(shell, "stat_links=");
  write_u64(shell, stat.link_count);
  write_newline(shell);

  write_string(shell, "stat_blocks=");
  write_u64(shell, stat.block_count);
  write_newline(shell);

  write_string(shell, "stat_mode=");
  write_u64(shell, stat.mode);
  write_newline(shell);

  for (size_t block_index = 0; block_index < kVfsDirectBlockCount; ++block_index) {
    if (stat.direct_blocks[block_index] == kVfsInvalidBlockIndex) {
      continue;
    }

    write_string(shell, "stat_block_");
    write_u64(shell, block_index);
    write_string(shell, "=");
    write_u64(shell, stat.direct_blocks[block_index]);
    write_newline(shell);
  }

  if (stat.indirect_block != kVfsInvalidBlockIndex) {
    write_string(shell, "stat_indirect_block=");
    write_u64(shell, stat.indirect_block);
    write_newline(shell);
  }
}

void handle_touch_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: touch <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "touch path too long");
    write_newline(shell);
    return;
  }

  VfsStat existing;
  const SyscallStatus status =
      sys_stat_path(shell->syscall_context, path, &existing);
  if (status == kSyscallOk) {
    if (existing.type != kVfsNodeTypeFile) {
      write_string(shell, "touch not a file: ");
      write_string(shell, path);
      write_newline(shell);
      return;
    }

    write_string(shell, "touch_resolved_path=");
    write_string(shell, resolved_path);
    write_newline(shell);
    write_string(shell, "touch_exists=1");
    write_newline(shell);
    return;
  }

  if (status != kSyscallNotFound || !vfs_create_file(shell->vfs, resolved_path)) {
    write_string(shell, "touch failed: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "touch_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);
  write_string(shell, "touch_exists=0");
  write_newline(shell);
}

void handle_mkdir_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: mkdir <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "mkdir path too long");
    write_newline(shell);
    return;
  }

  VfsStat existing;
  if (sys_stat_path(shell->syscall_context, path, &existing) == kSyscallOk) {
    write_string(shell, "mkdir exists: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (!vfs_create_directory(shell->vfs, resolved_path)) {
    write_string(shell, "mkdir failed: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "mkdir_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);
}

void handle_write_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  char path[kSyscallPathCapacity];
  const char* text = nullptr;
  if (!split_path_and_text(arguments, path, sizeof(path), &text)) {
    write_string(shell, "usage: write <path> <text>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "write path too long");
    write_newline(shell);
    return;
  }

  const size_t text_length = string_length(text);
  if (!vfs_write_file(shell->vfs, resolved_path, text, text_length)) {
    write_string(shell, "write failed: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "write_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);
  write_string(shell, "write_bytes=");
  write_u64(shell, text_length);
  write_newline(shell);
}

void handle_append_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  char path[kSyscallPathCapacity];
  const char* text = nullptr;
  if (!split_path_and_text(arguments, path, sizeof(path), &text)) {
    write_string(shell, "usage: append <path> <text>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "append path too long");
    write_newline(shell);
    return;
  }

  const size_t text_length = string_length(text);
  if (!vfs_append_file(shell->vfs, resolved_path, text, text_length)) {
    write_string(shell, "append failed: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "append_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);
  write_string(shell, "append_bytes=");
  write_u64(shell, text_length);
  write_newline(shell);
}

void handle_rm_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: rm <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "rm path too long");
    write_newline(shell);
    return;
  }

  if (!vfs_unlink(shell->vfs, resolved_path)) {
    write_string(shell, "rm failed: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  write_string(shell, "rm_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);
}

void handle_sync_command(ShellState* shell) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  if (!vfs_sync(shell->vfs)) {
    write_string(shell, "sync failed");
    write_newline(shell);
    return;
  }

  write_string(shell, "sync ok");
  write_newline(shell);
}

void handle_run_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr ||
      shell->allocator == nullptr ||
      shell->filesystem == nullptr ||
      shell->vfs == nullptr ||
      shell->scheduler == nullptr ||
      !syscall_context_is_ready(shell->syscall_context) ||
      !scheduler_is_ready(shell->scheduler) ||
      !vfs_is_mounted(shell->vfs)) {
    write_string(shell, "run unavailable");
    write_newline(shell);
    return;
  }

  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    write_string(shell, "usage: run <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    write_string(shell, "run path too long");
    write_newline(shell);
    return;
  }

  VfsStat stat;
  const SyscallStatus stat_status =
      sys_stat_path(shell->syscall_context, path, &stat);
  if (stat_status == kSyscallNotFound) {
    write_string(shell, "run path not found: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  if (stat_status != kSyscallOk || stat.type != kVfsNodeTypeFile) {
    write_string(shell, "run not a file: ");
    write_string(shell, path);
    write_newline(shell);
    return;
  }

  SchedulerElfThreadLoadResult launch_result;
  memory_set(&launch_result, 0, sizeof(launch_result));
  const char* const process_name = path_leaf_name(resolved_path);
  if (!scheduler_create_user_elf_thread(
          shell->scheduler,
          shell->allocator,
          shell->filesystem,
          shell->vfs,
          shell->syscall_context->write_handler,
          shell->syscall_context->write_context,
          process_name,
          "main",
          resolved_path,
          kShellRunUserStackTop,
          kShellRunDefaultUserRflags,
          kThreadPriorityNormal,
          &launch_result)) {
    write_string(shell, "run load failed: ");
    write_string(shell, resolved_path);
    write_newline(shell);
    return;
  }

  write_string(shell, "run_path=");
  write_string(shell, path);
  write_newline(shell);

  write_string(shell, "run_resolved_path=");
  write_string(shell, resolved_path);
  write_newline(shell);

  write_string(shell, "run_inode=");
  write_u64(shell, launch_result.program.inode_number);
  write_newline(shell);

  write_string(shell, "run_pid=");
  write_u64(shell, launch_result.process->pid);
  write_newline(shell);

  write_string(shell, "run_tid=");
  write_u64(shell, launch_result.thread->tid);
  write_newline(shell);

  write_string(shell, "run_entry=0x");
  write_hex64(shell, launch_result.program.entry_point);
  write_newline(shell);

  write_string(shell, "run_stack_top=0x");
  write_hex64(shell, kShellRunUserStackTop);
  write_newline(shell);

  write_string(shell, "run_segment_count=");
  write_u64(shell, launch_result.program.loadable_segment_count);
  write_newline(shell);

  write_string(shell, "run_page_count=");
  write_u64(shell, launch_result.program.mapped_page_count);
  write_newline(shell);

  if (scheduler_current_thread(shell->scheduler) == nullptr) {
    (void)scheduler_run_until_idle(shell->scheduler);
  } else {
    (void)scheduler_yield_current_thread();
  }

  if (launch_result.thread->state == kThreadStateFinished) {
    const uint64_t return_value = launch_result.thread->user_mode.return_value;
    write_string(shell, "run_return_flags=0x");
    write_hex64(shell, return_value >> 16);
    write_newline(shell);
  }

  write_string(shell, "run_process_state=");
  write_string(shell,
               scheduler_process_state_name(launch_result.process->state));
  write_newline(shell);

  write_string(shell, "run_thread_state=");
  write_string(shell,
               scheduler_thread_state_name(launch_result.thread->state));
  write_newline(shell);
}

void handle_irq_command(const ShellState* shell) {
  write_string(shell, "irq_timer_ticks=");
  write_u64(shell, timer_tick_count());
  write_newline(shell);

  write_string(shell, "irq_timer_frequency_hz=");
  write_u64(shell, timer_frequency_hz());
  write_newline(shell);

  write_string(shell, "irq_keyboard_count=");
  write_u64(shell, keyboard_irq_count());
  write_newline(shell);

  write_string(shell, "irq_keyboard_buffered_chars=");
  write_u64(shell, keyboard_buffered_char_count());
  write_newline(shell);

  write_string(shell, "irq_keyboard_dropped_chars=");
  write_u64(shell, keyboard_dropped_char_count());
  write_newline(shell);
}

void handle_bootinfo_command(const ShellState* shell) {
  if (!is_boot_info_valid(shell != nullptr ? shell->boot_info : nullptr)) {
    write_string(shell, "bootinfo unavailable");
    write_newline(shell);
    return;
  }

  write_string(shell, "bootinfo_magic=0x");
  write_hex64(shell, shell->boot_info->magic);
  write_newline(shell);

  write_string(shell, "bootinfo_memory_map_count=");
  write_u64(shell, shell->boot_info->memory_map_count);
  write_newline(shell);

  write_string(shell, "bootinfo_memory_map_entry_size=");
  write_u64(shell, shell->boot_info->memory_map_entry_size);
  write_newline(shell);

  write_string(shell, "bootinfo_memory_map_ptr=0x");
  write_hex64(shell, shell->boot_info->memory_map_ptr);
  write_newline(shell);

  write_string(shell, "bootinfo_boot_volume_ptr=0x");
  write_hex64(shell, shell->boot_info->boot_volume_ptr);
  write_newline(shell);

  write_string(shell, "bootinfo_boot_volume_start_lba=");
  write_u64(shell, shell->boot_info->boot_volume_start_lba);
  write_newline(shell);

  write_string(shell, "bootinfo_boot_volume_sector_count=");
  write_u64(shell, shell->boot_info->boot_volume_sector_count);
  write_newline(shell);

  write_string(shell, "bootinfo_boot_volume_sector_size=");
  write_u64(shell, shell->boot_info->boot_volume_sector_size);
  write_newline(shell);
}

void handle_e820_command(const ShellState* shell) {
  if (!is_boot_info_valid(shell != nullptr ? shell->boot_info : nullptr)) {
    write_string(shell, "e820 unavailable");
    write_newline(shell);
    return;
  }

  const auto* entries = reinterpret_cast<const E820Entry*>(
      static_cast<uintptr_t>(shell->boot_info->memory_map_ptr));

  write_string(shell, "e820_count=");
  write_u64(shell, shell->boot_info->memory_map_count);
  write_newline(shell);

  for (uint16_t i = 0; i < shell->boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];

    write_string(shell, "e820_shell[");
    write_u64(shell, i);
    write_string(shell, "] base=0x");
    write_hex64(shell, entry.base);
    write_string(shell, " length=0x");
    write_hex64(shell, entry.length);
    write_string(shell, " raw_type=0x");
    write_hex64(shell, entry.type);
    write_string(shell, " kind=");
    write_string(shell, memory_kind_name(entry.type));
    write_newline(shell);
  }
}

void handle_cpu_command(const ShellState* shell) {
  const CpuidResult leaf0 = read_cpuid(0, 0);
  const CpuidResult extended_leaf = read_cpuid(0x80000000u, 0);

  char vendor[13];
  vendor[0] = static_cast<char>(leaf0.ebx & 0xFF);
  vendor[1] = static_cast<char>((leaf0.ebx >> 8) & 0xFF);
  vendor[2] = static_cast<char>((leaf0.ebx >> 16) & 0xFF);
  vendor[3] = static_cast<char>((leaf0.ebx >> 24) & 0xFF);
  vendor[4] = static_cast<char>(leaf0.edx & 0xFF);
  vendor[5] = static_cast<char>((leaf0.edx >> 8) & 0xFF);
  vendor[6] = static_cast<char>((leaf0.edx >> 16) & 0xFF);
  vendor[7] = static_cast<char>((leaf0.edx >> 24) & 0xFF);
  vendor[8] = static_cast<char>(leaf0.ecx & 0xFF);
  vendor[9] = static_cast<char>((leaf0.ecx >> 8) & 0xFF);
  vendor[10] = static_cast<char>((leaf0.ecx >> 16) & 0xFF);
  vendor[11] = static_cast<char>((leaf0.ecx >> 24) & 0xFF);
  vendor[12] = '\0';

  write_string(shell, "cpu_vendor=");
  write_string(shell, vendor);
  write_newline(shell);

  write_string(shell, "cpu_max_basic_leaf=0x");
  write_hex64(shell, leaf0.eax);
  write_newline(shell);

  write_string(shell, "cpu_max_extended_leaf=0x");
  write_hex64(shell, extended_leaf.eax);
  write_newline(shell);

  uint64_t long_mode = 0;
  if (extended_leaf.eax >= 0x80000001u) {
    const CpuidResult features = read_cpuid(0x80000001u, 0);
    long_mode = ((features.edx >> 29) & 0x1u);
  }

  write_string(shell, "cpu_long_mode=");
  write_u64(shell, long_mode);
  write_newline(shell);
}

void handle_uptime_command(const ShellState* shell) {
  const uint64_t ticks = timer_tick_count();
  const uint64_t frequency_hz = timer_frequency_hz();

  write_string(shell, "uptime_ticks=");
  write_u64(shell, ticks);
  write_newline(shell);

  write_string(shell, "uptime_frequency_hz=");
  write_u64(shell, frequency_hz);
  write_newline(shell);

  if (frequency_hz == 0) {
    write_string(shell, "uptime_ms=0");
    write_newline(shell);
    return;
  }

  write_string(shell, "uptime_ms=");
  write_u64(shell, (ticks * 1000) / frequency_hz);
  write_newline(shell);
}

void handle_echo_command(const ShellState* shell, const char* arguments) {
  const char* text = skip_spaces(arguments);
  if (text != nullptr) {
    write_string(shell, text);
  }

  write_newline(shell);
}

void handle_history_command(const ShellState* shell) {
  if (shell == nullptr) {
    return;
  }

  write_string(shell, "history_buffered=");
  write_u64(shell, shell->history_count);
  write_newline(shell);

  write_string(shell, "history_total=");
  write_u64(shell, shell->history_total_count);
  write_newline(shell);

  if (shell->history_count == 0) {
    write_string(shell, "history empty");
    write_newline(shell);
    return;
  }

  for (size_t i = 0; i < shell->history_count; ++i) {
    const size_t slot = history_slot_index(shell, i);
    write_string(shell, "history[");
    write_u64(shell, shell->history_sequence_numbers[slot]);
    write_string(shell, "]=");
    write_string(shell, shell->history_entries[slot]);
    write_newline(shell);
  }
}

}  // namespace

bool initialize_shell(ShellState* shell,
                      const BootInfo* boot_info,
                      PageAllocator* allocator,
                      const KernelHeap* heap,
                      const BootVolume* boot_volume,
                      const BlockDevice* block_device,
                      const Os64Fs* filesystem,
                      VfsMount* vfs,
                      SchedulerState* scheduler,
                      SyscallContext* syscall_context,
                      const ShellOutput* output) {
  if (shell == nullptr || output == nullptr || output->write_char == nullptr ||
      !syscall_context_is_ready(syscall_context)) {
    return false;
  }

  shell->boot_info = boot_info;
  shell->allocator = allocator;
  shell->heap = heap;
  shell->boot_volume = boot_volume;
  shell->block_device = block_device;
  shell->filesystem = filesystem;
  shell->vfs = vfs;
  shell->scheduler = scheduler;
  shell->syscall_context = syscall_context;
  shell->output = *output;
  shell->history_count = 0;
  shell->history_next_slot = 0;
  shell->history_total_count = 0;
  memory_set(shell->history_sequence_numbers, 0,
             sizeof(shell->history_sequence_numbers));
  memory_set(shell->history_entries, 0, sizeof(shell->history_entries));
  return sys_chdir(shell->syscall_context, "/") == kSyscallOk;
}

void shell_print_prompt(const ShellState* shell) {
  // macOS 默认 zsh 提示符习惯以 `%` 收尾，这里借这个视觉习惯把 shell 做得更像终端窗口。
  set_output_color(shell, shell->output.prompt_color);
  write_string(shell, "os64 % ");
  set_output_color(shell, shell->output.text_color);
}

size_t shell_history_entry_count(const ShellState* shell) {
  if (shell == nullptr) {
    return 0;
  }

  return shell->history_count;
}

const char* shell_history_entry_text(const ShellState* shell, size_t index) {
  if (shell == nullptr || index >= shell->history_count) {
    return nullptr;
  }

  const size_t slot = history_slot_index(shell, index);
  return shell->history_entries[slot];
}

ShellCommandResult shell_execute_line(ShellState* shell,
                                      const char* line) {
  const char* trimmed_line = skip_spaces(line);
  if (trimmed_line == nullptr || trimmed_line[0] == '\0') {
    return kShellCommandEmpty;
  }

  // 先记历史，再执行命令。
  // 这样 `history` 自己也会出现在当前历史列表里，更符合直觉。
  record_history_line(shell, trimmed_line);

  const char* arguments = nullptr;

  if (command_matches(trimmed_line, "help", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_help_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "mem", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_mem_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "ticks", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_ticks_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "heap", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_heap_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "disk", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_disk_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "pwd", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_pwd_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "cd", &arguments)) {
    handle_cd_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "ls", &arguments)) {
    handle_ls_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "cat", &arguments)) {
    handle_cat_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "stat", &arguments)) {
    handle_stat_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "touch", &arguments)) {
    handle_touch_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "mkdir", &arguments)) {
    handle_mkdir_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "write", &arguments)) {
    handle_write_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "append", &arguments)) {
    handle_append_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "rm", &arguments)) {
    handle_rm_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "sync", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_sync_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "run", &arguments)) {
    handle_run_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "irq", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_irq_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "bootinfo", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_bootinfo_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "e820", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_e820_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "cpu", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_cpu_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "uptime", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_uptime_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "echo", &arguments)) {
    handle_echo_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "history", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_history_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "clear", &arguments) &&
      is_empty_after_trim(arguments)) {
    clear_output(shell);
    return kShellCommandExecuted;
  }

  write_string(shell, "unknown command: ");
  write_string(shell, trimmed_line);
  write_newline(shell);
  return kShellCommandUnknown;
}

const char* shell_command_result_name(ShellCommandResult result) {
  switch (result) {
    case kShellCommandEmpty:
      return "empty";
    case kShellCommandExecuted:
      return "executed";
    case kShellCommandUnknown:
      return "unknown";
    default:
      return "invalid";
  }
}

ShellCommandResult shell_run_once(ShellState* shell,
                                  char* line_buffer,
                                  size_t capacity,
                                  size_t* out_line_length) {
  if (shell == nullptr || line_buffer == nullptr || capacity < 2) {
    if (out_line_length != nullptr) {
      *out_line_length = 0;
    }
    return kShellCommandEmpty;
  }

  ConsoleHistoryProvider history_provider;
  history_provider.entry_count = history_provider_entry_count;
  history_provider.entry_text = history_provider_entry_text;
  history_provider.context = shell;

  shell_print_prompt(shell);
  const size_t line_length =
      console_read_line_with_history(line_buffer, capacity, &history_provider);
  if (out_line_length != nullptr) {
    *out_line_length = line_length;
  }

  return shell_execute_line(shell, line_buffer);
}

void shell_run_forever(ShellState* shell,
                       char* line_buffer,
                       size_t capacity) {
  if (shell == nullptr || line_buffer == nullptr || capacity < 2) {
    return;
  }

  for (;;) {
    (void)shell_run_once(shell, line_buffer, capacity, nullptr);
  }
}
