#include "shell/shell.hpp"

#include "console/console.hpp"
#include "fs/os64fs.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pit.hpp"
#include "memory/kmemory.hpp"
#include "runtime/runtime.hpp"
#include "storage/boot_volume.hpp"
#include "task/scheduler.hpp"
#include "cpu/cpu.hpp"
#include "log/log.hpp"
#include "perf/perf.hpp"
#include "net/network.hpp"

namespace {

constexpr size_t kShellListDirCapacity = 128;
constexpr uint64_t kShellRunUserStackTop = 0x0000000000800000ULL;  // 先继续复用当前教学内核统一的用户栈顶。
constexpr uint64_t kShellRunDefaultUserRflags = 0x202ULL;          // bit1 恒为 1，并先把 IF 打开，让 shell 启动的用户程序也能继续接收外部 IRQ。

struct CpuidResult {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
};

// 下面这一组 helper 先统一 shell 的最小输出能力。
// shell 自己不直接操作 VGA 或串口，
// 它只知道“我要输出字符/字符串/数字”，真正写到哪里由外部注入。
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

  // 例如：
  // `/docs/hello.elf` -> `hello.elf`
  // 这样 `run` 在给新进程命名时，就能直接取文件名当默认进程名。
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

  // `write path text...` / `append path text...` 这类命令要先拆成两段：
  // 第一段当路径，剩余整段都保留成正文文本。
  const char* path_begin = skip_spaces(arguments);
  if (path_begin == nullptr || path_begin[0] == '\0') {
    return false;
  }

  // 这里故意只把“第一个空白前”的部分当路径，
  // 因为后面的整段文本要原样保留给 write/append 当文件内容。
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

  // 历史缓冲还没装满时，逻辑顺序和物理槽位顺序一样；
  // 装满以后，`history_next_slot` 就是“最老记录的下一个位置”，
  // 这时要把“按时间顺序的第 N 条”重新映射回 ring buffer 槽位。
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

  // console 层完全不知道 shell 内部 ring buffer 怎么排，
  // 它只通过这个 provider 拿“按时间顺序的第 N 条历史文本”。
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

  // 这里故意要求命令名后面要么结束，要么真的是空白。
  // 这样 `mem` 不会误匹配到 `memory`。
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
  write_string(shell, "run <path> [args] - run a user ELF and wait for exit");
  write_newline(shell);
  write_string(shell, "ps    - show processes and threads");
  write_newline(shell);
  write_string(shell, "reboot / shutdown - sync disk and restart / stop");
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
  write_string(shell, "commands: /bin PATH lookup; quotes; | < > >> 2> ; && || &\n");
  write_string(shell, "jobs / wait - list / join background pipelines\n");
  write_string(shell, "dmesg / logsave PATH - inspect / save the bounded kernel log\n");
  write_string(shell, "perf - CPU, memory and scheduler counters\n");
  write_string(shell, "net / ping IPv4 - network counters / ICMP echo\n");
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
  // `disk` 不是直接去读文件内容，
  // 它主要用来告诉你：当前 shell 背后到底挂的是哪块卷、多少扇区、文件系统统计如何。
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

  // `pwd` 故意不直接读 shell 私有字段，
  // 而是复用 syscall 层的 cwd 视图。
  // 这样以后从用户态发起 `getcwd` 时，和 shell 看到的语义就是同一套。
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

  // `cd` 的关键路径是：
  // raw input -> sys_chdir -> syscall_resolve_path + stat_path -> 更新 syscall_context.cwd
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

  // `ls` 这一步看起来像纯显示命令，
  // 实际上它完整穿过了：
  // shell -> syscall path resolve -> stat_path -> listdir -> VFS -> directory handle
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

  auto* entries = static_cast<VfsDirectoryEntry*>(
      kcalloc(entry_count == 0 ? 1 : entry_count, sizeof(VfsDirectoryEntry)));
  if (entries == nullptr) {
    write_string(shell, "ls out of memory");
    write_newline(shell);
    return;
  }
  const int32_t copied_count =
      sys_listdir(shell->syscall_context, path, entries,
                  static_cast<size_t>(entry_count));
  if (copied_count != entry_count) {
    kfree(entries);
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
  kfree(entries);
}

void handle_cat_command(const ShellState* shell, const char* arguments) {
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `cat` 的完整链路是：
  // shell path -> sys_open -> fd -> sys_read(loop) -> 逐块打印内容 -> sys_close
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

void handle_stat_command(ShellState* shell, const char* arguments) {
  if (shell != nullptr) { shell->last_status = 1; }
  if (shell == nullptr || !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `stat` 是最适合观察分层的命令之一：
  // 它不读文件内容，只把 VFS 看到的 inode / block / size / type 元数据打印出来。
  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    shell->last_status = 2;
    write_string(shell, "usage: stat <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    shell->last_status = 2;
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
  shell->last_status = 0;
}

void handle_touch_command(ShellState* shell, const char* arguments) {
  if (shell != nullptr) { shell->last_status = 1; }
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `touch` 这一版只负责“确保这个文件存在”。
  // 如果文件本来就有，它不会像 Unix 那样更新时间戳，因为当前文件系统还没有时间字段逻辑。
  const char* path = skip_spaces(arguments);
  if (path == nullptr || path[0] == '\0') {
    shell->last_status = 2;
    write_string(shell, "usage: touch <path>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    shell->last_status = 2;
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
    shell->last_status = 0;
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
  shell->last_status = 0;
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
  if (shell != nullptr) { shell->last_status = 1; }
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `write` 是覆盖写：
  // 路径后面的剩余文本全部原样作为文件新内容。
  char path[kSyscallPathCapacity];
  const char* text = nullptr;
  if (!split_path_and_text(arguments, path, sizeof(path), &text)) {
    shell->last_status = 2;
    write_string(shell, "usage: write <path> <text>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    shell->last_status = 2;
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
  shell->last_status = 0;
}

void handle_append_command(ShellState* shell, const char* arguments) {
  if (shell != nullptr) { shell->last_status = 1; }
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `append` 和 `write` 的解析形状一样，
  // 唯一区别是底层落到 `vfs_append_file`，把内容拼到文件末尾。
  char path[kSyscallPathCapacity];
  const char* text = nullptr;
  if (!split_path_and_text(arguments, path, sizeof(path), &text)) {
    shell->last_status = 2;
    write_string(shell, "usage: append <path> <text>");
    write_newline(shell);
    return;
  }

  char resolved_path[kSyscallPathCapacity];
  if (!syscall_resolve_path(shell->syscall_context, path, resolved_path,
                            sizeof(resolved_path))) {
    shell->last_status = 2;
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
  shell->last_status = 0;
}

void handle_rm_command(ShellState* shell, const char* arguments) {
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs) ||
      !syscall_context_is_ready(shell->syscall_context)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // 当前 `rm` 统一走 `vfs_unlink`，
  // 底层再决定目标是普通文件还是空目录，以及是否允许删除。
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

  if (sys_unlink(shell->syscall_context, resolved_path) != kSyscallOk) {
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
  if (shell != nullptr) { shell->last_status = 1; }
  if (shell == nullptr || shell->vfs == nullptr ||
      !vfs_is_mounted(shell->vfs)) {
    write_string(shell, "fs unavailable");
    write_newline(shell);
    return;
  }

  // `sync` 对当前教学文件系统很重要：
  // 它的意义不是“看起来成功了”，而是把 metadata 真正刷回卷，
  // 这样重挂载之后结果还在。
  if (!vfs_sync(shell->vfs)) {
    write_string(shell, "sync failed");
    write_newline(shell);
    return;
  }

  write_string(shell, "sync ok");
  write_newline(shell);
  shell->last_status = 0;
}

void handle_run_command(ShellState* shell, const char* arguments) {
  if (shell != nullptr) { shell->last_status = 1; }
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

  // `run` 是这版 shell 里最像“真正操作系统命令”的一条：
  // 它不是简单打印状态，而是把 ELF 文件真正装进用户地址空间，
  // 再交给 scheduler 作为一条 user thread 去运行。
  char parsed[8][kSyscallPathCapacity];
  const char* argument_values[8];
  size_t argument_count = 0;
  const char* cursor = skip_spaces(arguments);
  bool valid = cursor != nullptr;
  while (valid && cursor[0] != '\0') {
    if (argument_count == 8) { valid = false; break; }
    size_t length = 0;
    char quote = 0;
    while (cursor[0] != '\0' && (quote != 0 || !is_space_char(cursor[0]))) {
      char ch = *cursor++;
      if (ch == '\'' || ch == '"') {
        if (quote == 0) { quote = ch; continue; }
        if (quote == ch) { quote = 0; continue; }
      }
      if (ch == '\\' && cursor[0] != '\0') { ch = *cursor++; }
      if (length + 1 >= kSyscallPathCapacity) { valid = false; break; }
      parsed[argument_count][length++] = ch;
    }
    if (quote != 0) { valid = false; }
    parsed[argument_count][length] = '\0';
    argument_values[argument_count] = parsed[argument_count];
    ++argument_count;
    cursor = skip_spaces(cursor);
  }
  if (!valid || argument_count == 0 || parsed[0][0] == '\0') {
    write_string(shell, "usage: run <path> [args] (8 args, 63 bytes each)");
    write_newline(shell);
    return;
  }
  const char* path = parsed[0];

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
  // 这里真正进入的是：
  // shell -> scheduler_create_user_elf_thread ->
  // ELF loader -> address space mapping -> scheduler ready queue
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

  if (sys_chdir(&launch_result.process->syscall_context,
                  syscall_current_working_directory(shell->syscall_context)) != kSyscallOk ||
      !scheduler_prepare_user_arguments(launch_result.process, launch_result.thread,
                                          argument_count, argument_values)) {
    (void)scheduler_discard_process(shell->scheduler, launch_result.process->pid);
    write_string(shell, "run argument setup failed");
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

  // 如果此刻 shell 不是在某条调度线程里跑的，说明这是早期 smoke/直接调用路径，
  // 那就直接把调度器跑到 idle，直到这条用户线程结束。
  //
  // 如果 shell 自己已经挂在调度器里，那就只主动 yield 一次，
  // 让新创建的用户线程有机会接过 CPU。
  int64_t exit_status = 0;
  if (!scheduler_wait_process(shell->scheduler, launch_result.process->pid, &exit_status)) {
    write_string(shell, "run wait failed");
    write_newline(shell);
    return;
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
  write_string(shell, "run_exit_code=");
  shell->last_status = static_cast<uint32_t>(exit_status) & 255;
  if (exit_status < 0) {
    write_char(shell, '-');
    write_u64(shell, static_cast<uint64_t>(-(exit_status + 1)) + 1);
  } else {
    write_u64(shell, static_cast<uint64_t>(exit_status));
  }
  write_newline(shell);
  (void)scheduler_reap_process(shell->scheduler, launch_result.process->pid, nullptr);
}

void handle_ps_command(const ShellState* shell) {
  if (shell->scheduler == nullptr) { return; }
  write_string(shell, "PID PPID STATE THREADS TICKS NAME");
  write_newline(shell);
  for (const auto& process : shell->scheduler->processes) {
    if (!process.in_use) { continue; }
    write_u64(shell, process.pid); write_char(shell, ' ');
    write_u64(shell, process.parent_pid); write_char(shell, ' ');
    write_string(shell, scheduler_process_state_name(process.state)); write_char(shell, ' ');
    write_u64(shell, process.live_thread_count); write_char(shell, ' ');
    write_u64(shell, process.total_thread_ticks); write_char(shell, ' ');
    write_string(shell, process.name); write_newline(shell);
  }
}

void handle_power_command(ShellState* shell, bool restart) {
  if (sys_sync(shell->syscall_context) != kSyscallOk) {
    write_string(shell, "disk sync failed; power operation cancelled");
    write_newline(shell);
    return;
  }
  write_string(shell, restart ? "rebooting" : "shutdown ok");
  write_newline(shell);
  disable_interrupts();
  if (restart) {
    for (unsigned i = 0; i < 100000; ++i) {
      uint8_t status;
      asm volatile("inb %1, %0" : "=a"(status) : "Nd"(static_cast<uint16_t>(0x64)));
      if ((status & 2) == 0) {
        asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0xFE)),
                     "Nd"(static_cast<uint16_t>(0x64)));
        break;
      }
    }
  } else {
    asm volatile("outw %0, %1" : : "a"(static_cast<uint16_t>(0x2000)),
                 "Nd"(static_cast<uint16_t>(0x604)));
    asm volatile("outw %0, %1" : : "a"(static_cast<uint16_t>(0x2000)),
                 "Nd"(static_cast<uint16_t>(0xB004)));
  }
  for (;;) { wait_for_interrupt(); }
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

  // shell 自己并不“拥有”这些大对象，
  // 它只是把启动阶段已经准备好的各层对象接到一起。
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
  shell->last_status = 0;
  shell->next_job_id = 1;
  memory_set(shell->jobs, 0, sizeof(shell->jobs));
  shell->history_count = 0;
  shell->history_next_slot = 0;
  shell->history_total_count = 0;
  memory_set(shell->history_sequence_numbers, 0,
             sizeof(shell->history_sequence_numbers));
  memory_set(shell->history_entries, 0, sizeof(shell->history_entries));

  // 启动后先把 cwd 归一到 `/`，这样 shell 和用户态第一眼看到的工作目录一致。
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

static ShellCommandResult shell_execute_legacy_line(ShellState* shell,
                                      const char* line) {
  const char* trimmed_line = skip_spaces(line);
  if (trimmed_line == nullptr || trimmed_line[0] == '\0') {
    return kShellCommandEmpty;
  }

  // 先记历史，再执行命令。
  // 这样 `history` 自己也会出现在当前历史列表里，更符合直觉。

  const char* arguments = nullptr;

  // 这一长串 if 的本质是第一版命令分发表。
  // 现在先用最直接的线性匹配保持可读性，
  // 等命令继续变多时，再考虑命令表或函数指针数组。
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

  if (command_matches(trimmed_line, "ps", &arguments) && is_empty_after_trim(arguments)) {
    handle_ps_command(shell);
    return kShellCommandExecuted;
  }
  if (command_matches(trimmed_line, "shutdown", &arguments) && is_empty_after_trim(arguments)) {
    handle_power_command(shell, false);
    return kShellCommandExecuted;
  }
  if (command_matches(trimmed_line, "reboot", &arguments) && is_empty_after_trim(arguments)) {
    handle_power_command(shell, true);
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
    // `clear` 是少数不走 syscall/VFS 的命令，
    // 它直接调用输出后端的 clear 回调，把 console 视口清空。
    clear_output(shell);
    return kShellCommandExecuted;
  }

  write_string(shell, "unknown command: ");
  write_string(shell, trimmed_line);
  write_newline(shell);
  return kShellCommandUnknown;
}

namespace {
bool same_word(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
bool legacy_builtin(const char* name) {
  const char* names[] = {"help", "mem", "ticks", "heap", "disk", "pwd", "cd", "ls",
    "cat", "stat", "touch", "mkdir", "write", "append", "rm", "sync", "run", "ps",
    "shutdown", "reboot", "irq", "bootinfo", "e820", "cpu", "uptime", "echo", "history", "clear"};
  for (const char* builtin : names) { if (same_word(name, builtin)) { return true; } }
  return false;
}
void show_performance(const ShellState* shell) {
  PerformanceSnapshot snapshot; performance_snapshot(&snapshot);
  const CpuInformation& cpu = cpu_information();
  write_string(shell, "cpu_brand="); write_string(shell, cpu.brand); write_newline(shell);
  write_string(shell, "cpu_fpu_sse_state="); write_u64(shell, cpu.floating_state_enabled); write_newline(shell);
  const char* names[] = {"perf_abi", "perf_ticks", "perf_timer_hz", "perf_switches", "perf_yields",
    "perf_preempt_requests", "perf_free_pages", "perf_heap_used_bytes", "perf_live_threads",
    "perf_ready_threads", "perf_blocked_threads", "perf_sleeping_threads", "perf_log_next_sequence",
    "perf_log_overwritten", "perf_cpu_features", "perf_tsc_raw"};
  const uint64_t values[] = {snapshot.abi_version, snapshot.ticks, snapshot.timer_hz, snapshot.context_switches,
    snapshot.yields, snapshot.preempt_requests, snapshot.free_pages, snapshot.heap_used_bytes,
    snapshot.live_threads, snapshot.ready_threads, snapshot.blocked_threads, snapshot.sleeping_threads,
    snapshot.log_next_sequence, snapshot.log_overwritten, snapshot.cpu_features, snapshot.tsc};
  for (size_t i = 0; i < 16; ++i) {
    write_string(shell, names[i]); write_char(shell, '='); write_u64(shell, values[i]); write_newline(shell);
  }
}
void show_network(const ShellState* shell) {
  const NetworkStatus& status = network_status();
  const VirtioNetStatus& device = virtio_net_status();
  write_string(shell, "network_ready="); write_u64(shell, status.ready); write_newline(shell);
  if (!status.ready) { return; }
  write_string(shell, "network_driver=virtio-net\n");
  write_string(shell, "network_rx_buffers="); write_u64(shell, device.rx_buffer_count); write_newline(shell);
  write_string(shell, "network_tx_buffers="); write_u64(shell, device.tx_buffer_count); write_newline(shell);
  write_string(shell, "network_irq_enabled="); write_u64(shell, device.irq_enabled); write_newline(shell);
  write_string(shell, "network_irq_line="); write_u64(shell, device.irq_line); write_newline(shell);
  write_string(shell, "network_irq_count="); write_u64(shell, device.irq_count); write_newline(shell);
  char address[16]; network_format_ipv4(status.address, address, sizeof(address));
  write_string(shell, "network_ip="); write_string(shell, address); write_newline(shell);
  network_format_ipv4(status.gateway, address, sizeof(address));
  write_string(shell, "network_gateway="); write_string(shell, address); write_newline(shell);
  const char* names[] = {"network_echo_port", "network_rx_packets", "network_tx_packets", "network_rx_bytes",
    "network_tx_bytes", "network_rx_dropped", "network_tx_busy", "network_device_errors", "network_invalid",
    "network_unsupported", "network_udp_received", "network_udp_sent", "network_udp_dropped", "network_udp_echoed"};
  const uint64_t values[] = {status.echo_port, device.rx_packets, device.tx_packets, device.rx_bytes, device.tx_bytes,
    device.rx_dropped, device.tx_busy, device.device_errors, status.invalid_packets, status.unsupported_packets,
    status.udp_received, status.udp_sent, status.udp_dropped, status.udp_echoed};
  for (size_t i = 0; i < 14; ++i) {
    write_string(shell, names[i]); write_char(shell, '='); write_u64(shell, values[i]); write_newline(shell);
  }
}
int32_t ping_command(ShellState* shell, const ShellParsedCommand& command) {
  uint32_t address;
  if (command.argc != 2 || !network_parse_ipv4(command.argv[1], &address)) {
    write_string(shell, "usage: ping IPv4 (one request)\n"); return 2;
  }
  NetworkPingResult result{}; const bool ok = network_ping(address, 1000, &result);
  write_string(shell, "ping_sent="); write_u64(shell, result.sent); write_newline(shell);
  write_string(shell, "ping_received="); write_u64(shell, result.replied); write_newline(shell);
  write_string(shell, "ping_rtt_ms="); write_u64(shell, result.round_trip_ms); write_newline(shell);
  return ok && result.replied ? 0 : 1;
}
size_t append_log_number(char* text, size_t cursor, uint64_t value) {
  char digits[20]; size_t count = 0;
  do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
  while (count) { text[cursor++] = digits[--count]; }
  return cursor;
}
size_t append_log_text(char* text, size_t cursor, const char* source) {
  while (*source) { text[cursor++] = *source++; } return cursor;
}
size_t format_log_record(char* text, const KernelLogRecord& record) {
  size_t size = append_log_number(text, 0, record.sequence); text[size++] = ' ';
  size = append_log_number(text, size, record.ticks); text[size++] = ' ';
  size = append_log_text(text, size, kernel_log_level_name(record.level)); text[size++] = ' ';
  size = append_log_text(text, size, record.component); text[size++] = ' ';
  size = append_log_text(text, size, record.message); text[size++] = '\n'; text[size] = 0;
  return size;
}
int32_t show_or_save_log(ShellState* shell, const char* path) {
  // Snapshot the bounded ring once. Formatting and disk I/O happen with IRQs
  // enabled, after the log lock is released, and never from an interrupt.
  auto* records = static_cast<KernelLogRecord*>(kmalloc(sizeof(KernelLogRecord) * kKernelLogCapacity));
  if (!records) { return 1; }
  const size_t count = kernel_log_read(0, records, kKernelLogCapacity);
  char* text = path ? static_cast<char*>(kmalloc(kKernelLogCapacity * 176 + 1)) : nullptr;
  if (path && !text) { kfree(records); return 1; }
  size_t size = 0;
  for (size_t i = 0; i < count; ++i) {
    char line[176]; const size_t bytes = format_log_record(line, records[i]);
    if (path) { memory_copy(text + size, line, bytes); size += bytes; }
    else { write_string(shell, line); }
  }
  bool ok = true;
  if (path) {
    ok = sys_replace_file(shell->syscall_context, path, text, size) == static_cast<int32_t>(size) &&
         vfs_sync(shell->vfs);
    write_string(shell, ok ? "logsave ok\n" : "logsave failed\n"); kfree(text);
  } else {
    const KernelLogStats stats = kernel_log_stats();
    write_string(shell, "log_overwritten="); write_u64(shell, stats.overwritten); write_newline(shell);
  }
  kfree(records); return ok ? 0 : 1;
}
bool redirect_present(const ShellParsedCommand& command) {
  return command.input[0] || command.output[0] || command.error[0];
}
void refresh_job(ShellState* shell, ShellJob& job, bool wait) {
  if (!job.in_use || job.done) { return; }
  bool remaining = false;
  for (size_t i = 0; i < job.count; ++i) {
    if (!job.pids[i]) { continue; }
    ProcessControlBlock* process = scheduler_find_process(shell->scheduler, job.pids[i]);
    if (!wait && process && process->state != kProcessStateExited) { remaining = true; continue; }
    int32_t status = 127;
    if (sys_waitpid(static_cast<int32_t>(job.pids[i]), &status) < 0) { status = 127; }
    if (i + 1 == job.count) { job.status = status; }
    job.pids[i] = 0;
  }
  job.done = !remaining;
}
int32_t jobs_command(ShellState* shell, bool wait) {
  int32_t status = 0;
  for (auto& job : shell->jobs) {
    if (!job.in_use) { continue; }
    refresh_job(shell, job, wait);
    if (wait) { status = job.status; job = {}; continue; }
    write_char(shell, '['); write_u64(shell, job.id); write_string(shell, "] ");
    write_string(shell, job.done ? "done " : "running ");
    write_string(shell, job.name); write_newline(shell);
    if (job.done) { job = {}; }
  }
  return status;
}
// Save shell streams with real reference-counted duplicates. A child receives
// these settings through spawn; restoring the parent does not undo the child.
bool restore_streams(ShellState* shell, const int32_t saved[3]) {
  bool ok = true;
  for (int32_t stream = 0; stream < 3; ++stream) {
    if (saved[stream] >= 0 && sys_dup2(shell->syscall_context, saved[stream], stream) < 0) { ok = false; }
  }
  return ok;
}
void close_descriptor(ShellState* shell, int32_t fd) {
  if (fd >= 0) { (void)sys_close(shell->syscall_context, fd); }
}
bool apply_redirect(ShellState* shell, const char* path, int32_t stream, bool append) {
  if (!*path) { return true; }
  const uint32_t flags = stream == 0 ? kOpenRead :
      kOpenWrite | kOpenCreate | (append ? kOpenAppend : kOpenTruncate);
  const int32_t fd = sys_open(shell->syscall_context, path, flags);
  if (fd < 0) { return false; }
  const bool ok = sys_dup2(shell->syscall_context, fd, stream) >= 0;
  close_descriptor(shell, fd); return ok;
}
int32_t launch_pipeline(ShellState* shell, ShellParsedLine* plan, const ShellParsedPipeline& pipeline) {
  char paths[kShellMaxPipelineCommands][kSyscallPathCapacity] = {};
  const char* arguments[kShellMaxPipelineCommands][kShellMaxArguments] = {};
  size_t argument_counts[kShellMaxPipelineCommands] = {};
  uint32_t pids[kShellMaxPipelineCommands] = {};
  ShellJob* job = nullptr;
  if (pipeline.background) {
    for (auto& entry : shell->jobs) {
      refresh_job(shell, entry, false);
      if (!entry.in_use || entry.done) { entry = {}; job = &entry; break; }
    }
    if (!job) { write_string(shell, "shell: job table full; use wait\n"); return 1; }
  }
  // Validate all executables before pipe creation and before truncating output.
  for (size_t i = 0; i < pipeline.count; ++i) {
    const auto& command = plan->commands[pipeline.first + i];
    const size_t first_argument = same_word(command.argv[0], "run") ? 1 : 0;
    if (command.argc <= first_argument) { write_string(shell, "shell: run needs a path\n"); return 2; }
    const char* program = command.argv[first_argument];
    bool slash = false; for (const char* p = program; *p; ++p) { slash |= *p == '/'; }
    char search[kSyscallPathCapacity] = {};
    if (!slash) {
      if (string_length(program) + 5 >= sizeof(search)) { return 127; }
      memory_copy(search, "/bin/", 5); memory_copy(search + 5, program, string_length(program) + 1);
      program = search;
    }
    if (!syscall_resolve_path(shell->syscall_context, program, paths[i], sizeof(paths[i]))) { return 127; }
    VfsStat stat;
    if (sys_stat_path(shell->syscall_context, paths[i], &stat) != kSyscallOk || stat.type != kVfsNodeTypeFile) {
      write_string(shell, "unknown command: "); write_string(shell, command.argv[first_argument]);
      write_newline(shell); return 127;
    }
    argument_counts[i] = command.argc - first_argument;
    for (size_t j = 0; j < argument_counts[i]; ++j) { arguments[i][j] = command.argv[first_argument + j]; }
  }
  int32_t saved[3] = {-1, -1, -1};
  int32_t pipes[kShellMaxPipelineCommands - 1][2];
  for (auto& pair : pipes) { pair[0] = -1; pair[1] = -1; }
  bool ok = true;
  for (int32_t stream = 0; stream < 3 && ok; ++stream) {
    saved[stream] = sys_dup(shell->syscall_context, stream); ok = saved[stream] >= 0;
  }
  for (size_t i = 0; i + 1 < pipeline.count && ok; ++i) {
    ok = sys_pipe(shell->syscall_context, pipes[i]) >= 0;
  }
  for (size_t i = 0; i < pipeline.count && ok; ++i) {
    ok = restore_streams(shell, saved);
    if (i && ok) { ok = sys_dup2(shell->syscall_context, pipes[i - 1][0], 0) >= 0; }
    if (i + 1 < pipeline.count && ok) { ok = sys_dup2(shell->syscall_context, pipes[i][1], 1) >= 0; }
    const auto& command = plan->commands[pipeline.first + i];
    if (ok) { ok = apply_redirect(shell, command.input, 0, false) &&
                  apply_redirect(shell, command.output, 1, command.output_append) &&
                  apply_redirect(shell, command.error, 2, command.error_append); }
    if (!ok) { break; }
    const int32_t pid = sys_spawn(shell->syscall_context, paths[i], arguments[i], argument_counts[i]);
    if (pid < 0) { ok = false; break; }
    pids[i] = static_cast<uint32_t>(pid);
    ProcessControlBlock* process = scheduler_find_process(shell->scheduler, pids[i]);
    // A shell command inherits just its three streams. Inheriting every spare
    // pipe endpoint would keep write references alive and prevent EOF forever.
    if (process) {
      for (int32_t fd = 3; fd < static_cast<int32_t>(kFileDescriptorCapacity + 3); ++fd) {
        (void)sys_close(&process->syscall_context, fd);
      }
    }
  }
  ok = restore_streams(shell, saved) && ok;
  for (int32_t fd : saved) { close_descriptor(shell, fd); }
  for (const auto& pair : pipes) { close_descriptor(shell, pair[0]); close_descriptor(shell, pair[1]); }
  if (!ok) {
    // No safe-point yield has occurred: created commands have not run yet.
    for (uint32_t pid : pids) { if (pid) { (void)scheduler_discard_process(shell->scheduler, pid); } }
    write_string(shell, "shell: pipeline setup failed\n"); return 1;
  }
  if (job) {
    job->in_use = true; job->id = shell->next_job_id++; job->count = pipeline.count;
    memory_copy(job->pids, pids, sizeof(pids));
    memory_copy(job->name, plan->commands[pipeline.first].argv[0], kShellArgumentBytes);
    write_char(shell, '['); write_u64(shell, job->id); write_string(shell, "] ");
    write_u64(shell, pids[pipeline.count - 1]); write_newline(shell); return 0;
  }
  int32_t status = 0;
  for (size_t i = 0; i < pipeline.count; ++i) {
    int32_t child_status = 127;
    if (sys_waitpid(static_cast<int32_t>(pids[i]), &child_status) < 0) { child_status = 127; }
    if (i + 1 == pipeline.count) { status = child_status; }
  }
  return status;
}
}

ShellCommandResult shell_execute_line(ShellState* shell, const char* line) {
  if (!shell || is_empty_after_trim(line)) { return kShellCommandEmpty; }
  record_history_line(shell, skip_spaces(line));
  auto* plan = static_cast<ShellParsedLine*>(kmalloc(sizeof(ShellParsedLine)));
  if (!plan) { write_string(shell, "shell: no parser memory\n"); shell->last_status = 1; return kShellCommandExecuted; }
  const char* error = nullptr;
  const ShellExpansion deferred{syscall_current_working_directory(shell->syscall_context), shell->last_status, true};
  if (!shell_parse_line(line, deferred, plan, &error)) {
    write_string(shell, "shell: "); write_string(shell, error ? error : "invalid syntax"); write_newline(shell);
    shell->last_status = 2; kfree(plan); return kShellCommandExecuted;
  }
  for (size_t p = 0; p < plan->pipeline_count; ++p) {
    const auto& pipeline = plan->pipelines[p];
    if ((pipeline.condition == kShellOnSuccess && shell->last_status != 0) ||
        (pipeline.condition == kShellOnFailure && shell->last_status == 0)) { continue; }
    bool expanded = true;
    const ShellExpansion variables{syscall_current_working_directory(shell->syscall_context), shell->last_status};
    for (size_t c = 0; c < pipeline.count; ++c) {
      expanded &= shell_expand_command(&plan->commands[pipeline.first + c], variables);
    }
    if (!expanded) { write_string(shell, "shell: expanded word exceeds 63 bytes\n"); shell->last_status = 2; continue; }
    const auto& command = plan->commands[pipeline.first];
    const bool plain = pipeline.count == 1 && !pipeline.background && !redirect_present(command);
    if (plain && same_word(command.argv[0], "net")) {
      if (command.argc == 1) { show_network(shell); shell->last_status = network_status().ready ? 0 : 1; }
      else { write_string(shell, "usage: net\n"); shell->last_status = 2; }
      continue;
    }
    if (plain && same_word(command.argv[0], "ping")) {
      shell->last_status = static_cast<uint32_t>(ping_command(shell, command)); continue;
    }
    if (plain && same_word(command.argv[0], "perf")) {
      if (command.argc == 1) { show_performance(shell); shell->last_status = 0; }
      else { write_string(shell, "usage: perf\n"); shell->last_status = 2; }
      continue;
    }
    if (plain && (same_word(command.argv[0], "dmesg") || same_word(command.argv[0], "logsave"))) {
      const bool save = same_word(command.argv[0], "logsave");
      if (command.argc != (save ? 2U : 1U)) {
        write_string(shell, "usage: dmesg | logsave PATH\n"); shell->last_status = 2;
      } else { shell->last_status = show_or_save_log(shell, save ? command.argv[1] : nullptr); }
      continue;
    }
    if (plain && same_word(command.argv[0], "true")) { shell->last_status = 0; continue; }
    if (plain && same_word(command.argv[0], "false")) { shell->last_status = 1; continue; }
    if (plain && (same_word(command.argv[0], "jobs") || same_word(command.argv[0], "wait"))) {
      if (command.argc != 1) { write_string(shell, "usage: jobs | wait\n"); shell->last_status = 2; }
      else { shell->last_status = static_cast<uint32_t>(jobs_command(shell, same_word(command.argv[0], "wait"))) & 255; }
      continue;
    }
    if (plain && same_word(command.argv[0], "cd")) {
      shell->last_status = command.argc > 2 ? 2 :
          sys_chdir(shell->syscall_context, command.argc == 1 ? "/" : command.argv[1]) == kSyscallOk ? 0 : 1;
      if (shell->last_status) { write_string(shell, "cd: directory unavailable\n"); }
      else if (scheduler_active_thread() == nullptr) {
        handle_cd_command(shell, syscall_current_working_directory(shell->syscall_context));
      }
      continue;
    }
    const bool standard_tool = same_word(command.argv[0], "cat") || same_word(command.argv[0], "echo") ||
        same_word(command.argv[0], "pwd") || same_word(command.argv[0], "ls") ||
        same_word(command.argv[0], "mkdir") || same_word(command.argv[0], "rm");
    // Boot smoke exercises retain verbose teaching commands against the RAM
    // fixture. The live terminal executes ordinary tools in isolated processes.
    const bool live_standard_tool = standard_tool && scheduler_active_thread() != nullptr;
    if (plain && legacy_builtin(command.argv[0]) && !live_standard_tool) {
      char normalized[kShellMaxInputBytes + 1] = {}; size_t length = 0;
      // Legacy run has its own argument lexer. Quote each decoded argument so
      // its diagnostic teaching output remains compatible with old exercises.
      const bool run = same_word(command.argv[0], "run");
      bool valid = true;
      for (size_t i = 0; i < command.argc && valid; ++i) {
        if (i) { normalized[length++] = ' '; }
        if (run && i) { normalized[length++] = '"'; }
        for (const char* word = command.argv[i]; *word; ++word) {
          const bool escaped = run && i && (*word == '"' || *word == '\\');
          if (length + (escaped ? 2 : 1) + 2 >= sizeof(normalized)) { valid = false; break; }
          if (escaped) { normalized[length++] = '\\'; }
          normalized[length++] = *word;
        }
        if (run && i && valid) { normalized[length++] = '"'; }
      }
      if (!valid) { shell->last_status = 2; continue; }
      shell->last_status = 0;
      if (shell_execute_legacy_line(shell, normalized) == kShellCommandUnknown) { shell->last_status = 127; }
    } else {
      shell->last_status = static_cast<uint32_t>(launch_pipeline(shell, plan, pipeline)) & 255;
    }
  }
  kfree(plan);
  return shell->last_status == 127 ? kShellCommandUnknown : kShellCommandExecuted;
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
  // console 层不直接依赖 shell 内部历史数组，
  // 而是通过 provider 回调拿“有多少条历史、每条是什么”。
  history_provider.entry_count = history_provider_entry_count;
  history_provider.entry_text = history_provider_entry_text;
  history_provider.context = shell;

  // 这一层把“交互式 shell 的一轮生命周期”固定成 3 步：
  // 1. 打提示符
  // 2. 读一行（带历史/编辑）
  // 3. 执行这一行
  shell_print_prompt(shell);
  const size_t line_length =
      console_read_line_with_history(line_buffer, capacity, &history_provider);
  if (out_line_length != nullptr) {
    *out_line_length = line_length;
  }

  if (line_length == kConsoleLineTooLong) {
    write_string(shell, "shell: input line too long; entire line discarded\n");
    shell->last_status = 2;
    return kShellCommandExecuted;
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
    // 这一层故意很薄：真正的一轮交互逻辑已经收口到 shell_run_once。
    (void)shell_run_once(shell, line_buffer, capacity, nullptr);
  }
}
