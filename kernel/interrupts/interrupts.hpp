#ifndef OS64_INTERRUPTS_HPP
#define OS64_INTERRUPTS_HPP

#include <stdbool.h>
#include <stdint.h>

constexpr uint8_t kCpuExceptionCount = 32;   // CPU 保留给异常/陷阱的前 32 个向量号。

// 这是我们约定给 C++ 异常处理函数看的最小中断栈帧。
// 现在先只关心：
// 1. 是哪一个异常向量触发了
// 2. CPU 自动压栈的 error code 是多少
// 3. 当时要执行的 RIP/CS/RFLAGS 是什么
struct InterruptFrame {
  uint64_t vector;       // 哪一个异常号触发了，比如 14 表示 page fault。
  uint64_t error_code;   // 某些异常会自动压入错误码，没有错误码的异常这里人为补 0。
  uint64_t rip;          // 当时 CPU 正准备执行的那条指令地址。
  uint64_t cs;           // 当时的代码段选择子。
  uint64_t rflags;       // 当时的标志寄存器值。
};

bool initialize_idt();
const char* exception_name(uint64_t vector);

static_assert(sizeof(InterruptFrame) == 40,
              "InterruptFrame layout must stay stable");

#endif
