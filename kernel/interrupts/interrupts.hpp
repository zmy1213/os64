#ifndef OS64_INTERRUPTS_HPP
#define OS64_INTERRUPTS_HPP

#include <stdbool.h>
#include <stdint.h>

constexpr uint8_t kCpuExceptionCount = 32;   // CPU 保留给异常/陷阱的前 32 个向量号。
constexpr uint8_t kHardwareIrqCount = 16;    // 传统 PIC 一共管理 16 路硬件 IRQ。
constexpr uint8_t kPicMasterVectorBase = 32; // PIC 重映射后，主片 IRQ0~7 从 32 开始。
constexpr uint8_t kPicSlaveVectorBase = 40;  // 从片 IRQ8~15 从 40 开始。
constexpr uint8_t kSyscallInterruptVector = 0x80;  // 第一版软中断 syscall 先约定走 `int 0x80`。

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

// `sti`：打开可屏蔽中断。
// 只有 IDT、PIC、PIT 都准备好之后才应该开。
void enable_interrupts();

// `cli`：关闭可屏蔽中断。
// 这一轮测试跑完后先关掉，避免后面日志阶段继续被时钟打断。
void disable_interrupts();

// 读取当前 IF 标志位，看看 CPU 此刻会不会接可屏蔽中断。
// 这对第一版 stdin/read(0) 很有用，因为“是否能等键盘 IRQ 把字符送进来”
// 取决于 IF 现在是不是开着。
bool interrupts_are_enabled();

// `hlt`：让 CPU 睡眠，直到下一次中断到来再醒。
// 这是最小内核里等待 tick 的最好办法之一，因为它不会忙等空转。
void wait_for_interrupt();

static_assert(sizeof(InterruptFrame) == 40,
              "InterruptFrame layout must stay stable");

#endif
