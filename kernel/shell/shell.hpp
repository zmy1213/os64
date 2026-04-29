#ifndef OS64_SHELL_HPP
#define OS64_SHELL_HPP

#include <stddef.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

// shell 只要求外界提供一个“输出 1 个字符”的最小能力。
// 这样它就不用关心自己是在写 VGA、串口，还是两边一起写。
struct ShellOutput {
  void (*write_char)(char ch);
};

struct ShellState {
  const PageAllocator* allocator;  // `mem` 命令会从这里拿当前页分配器状态。
  ShellOutput output;              // 所有 shell 输出最终都走这个回调。
};

enum ShellCommandResult : uint8_t {
  kShellCommandEmpty = 0,      // 输入是空行。
  kShellCommandExecuted = 1,   // 命令已识别并执行。
  kShellCommandUnknown = 2,    // 输入不是当前支持的内建命令。
};

bool initialize_shell(ShellState* shell,
                      const PageAllocator* allocator,
                      const ShellOutput* output);

// 打印最小提示符，让用户知道 shell 已经在等输入了。
void shell_print_prompt(const ShellState* shell);

// 执行一整行命令。
// 这一轮先只做最小内建命令：
// - help
// - mem
// - ticks
ShellCommandResult shell_execute_line(const ShellState* shell,
                                      const char* line);

// 返回一个稳定字符串，方便日志里打印这次命令结果。
const char* shell_command_result_name(ShellCommandResult result);

// 正常启动时的最小交互循环。
// 它会：
// 1. 打提示符
// 2. 读一行
// 3. 执行命令
// 4. 再继续等下一行
void shell_run_forever(const ShellState* shell,
                       char* line_buffer,
                       size_t capacity);

#endif
