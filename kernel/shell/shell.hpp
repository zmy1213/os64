#ifndef OS64_SHELL_HPP
#define OS64_SHELL_HPP

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"
#include "memory/heap.hpp"
#include "memory/page_allocator.hpp"
#include "storage/boot_volume.hpp"

constexpr size_t kShellHistoryCapacity = 16;       // 第一版先记最近 16 条命令，够演示 ring buffer。
constexpr size_t kShellHistoryEntryCapacity = 32;  // 和当前 shell 输入缓冲区保持同量级，先不做超长命令历史。

// shell 只要求外界提供一个“输出 1 个字符”的最小能力。
// 这样它就不用关心自己是在写 VGA、串口，还是两边一起写。
struct ShellOutput {
  void (*write_char)(char ch);
  void (*clear)();               // `clear` 命令用这个回调去清空控制台区域。
  void (*set_color)(uint8_t color);  // 提示符和普通输出可以用不同颜色，但 shell 仍然不直接碰 VGA。
  uint8_t text_color;                // 普通 shell 文本颜色。
  uint8_t prompt_color;              // 提示符颜色，单独做轻一点的强调。
};

struct ShellState {
  const BootInfo* boot_info;        // `bootinfo` / `e820` 命令要从这里看启动阶段交进来的信息。
  const PageAllocator* allocator;  // `mem` 命令会从这里拿当前页分配器状态。
  const KernelHeap* heap;          // `heap` 命令会从这里拿当前堆状态。
  const BootVolume* boot_volume;   // `disk` 命令会从这里看 stage2 预读进来的启动卷信息。
  ShellOutput output;              // 所有 shell 输出最终都走这个回调。
  uint16_t history_count;          // 当前 ring buffer 里实际存了多少条命令。
  uint16_t history_next_slot;      // 下一条命令应该写进哪个槽位。
  uint64_t history_total_count;    // 自 shell 初始化以来，一共执行过多少条非空命令。
  uint64_t history_sequence_numbers[kShellHistoryCapacity];
  char history_entries[kShellHistoryCapacity][kShellHistoryEntryCapacity];
};

enum ShellCommandResult : uint8_t {
  kShellCommandEmpty = 0,      // 输入是空行。
  kShellCommandExecuted = 1,   // 命令已识别并执行。
  kShellCommandUnknown = 2,    // 输入不是当前支持的内建命令。
};

bool initialize_shell(ShellState* shell,
                      const BootInfo* boot_info,
                      const PageAllocator* allocator,
                      const KernelHeap* heap,
                      const BootVolume* boot_volume,
                      const ShellOutput* output);

// 打印最小提示符，让用户知道 shell 已经在等输入了。
void shell_print_prompt(const ShellState* shell);

// 执行一整行命令。
// 现在这版 shell 先支持这些最小内建命令：
// - help
// - mem
// - ticks
// - heap
// - disk
// - irq
// - bootinfo
// - e820
// - cpu
// - uptime
// - echo <text>
// - history
// - clear
ShellCommandResult shell_execute_line(ShellState* shell,
                                      const char* line);

// 让控制台层能按“从旧到新”的顺序访问 shell 历史。
size_t shell_history_entry_count(const ShellState* shell);
const char* shell_history_entry_text(const ShellState* shell, size_t index);

// 返回一个稳定字符串，方便日志里打印这次命令结果。
const char* shell_command_result_name(ShellCommandResult result);

// 正常启动时的最小交互循环。
// 它会：
// 1. 打提示符
// 2. 读一行
// 3. 执行命令
// 4. 再继续等下一行
void shell_run_forever(ShellState* shell,
                       char* line_buffer,
                       size_t capacity);

#endif
