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

// 这是给“需要看到完整通用寄存器现场”的入口准备的帧。
// 现在有两类路径会用它：
// 1. `int 0x80` syscall
// 2. 硬件 IRQ，尤其是后面要做的“用户态被 timer 打断再恢复”
//
// 顺序必须严格匹配 `interrupt_stubs.asm` 里压栈的顺序。
// 也就是说，C++ 看到的不是“抽象出来的寄存器对象”，
// 而是真正照着汇编栈布局解释的一块内存。
struct RegisterInterruptFrame {
  uint64_t r15;          // 通用寄存器 r15。用户态/内核态切换回来以后，这些值都应该原样恢复。
  uint64_t r14;          // 通用寄存器 r14。
  uint64_t r13;          // 通用寄存器 r13。
  uint64_t r12;          // 通用寄存器 r12。
  uint64_t r11;          // 通用寄存器 r11。
  uint64_t r10;          // 通用寄存器 r10。
  uint64_t r9;           // 通用寄存器 r9。
  uint64_t r8;           // 通用寄存器 r8。
  uint64_t rdi;          // SysV 第 1 个参数寄存器。
  uint64_t rsi;          // SysV 第 2 个参数寄存器。
  uint64_t rbp;          // 基址寄存器；很多函数会把它当栈帧锚点。
  uint64_t rbx;          // 被调用者保存寄存器之一。
  uint64_t rdx;          // SysV 第 3 个参数寄存器。
  uint64_t rcx;          // SysV 第 4 个参数寄存器；当前 `int 0x80` ABI 也拿它当第 4 参数。
  uint64_t rax;          // 常常既是 syscall 编号寄存器，也是返回值寄存器。
  uint64_t vector;       // 这次进内核到底是哪个向量触发的，比如 32 是 timer IRQ，128 是 `int 0x80`。
  uint64_t error_code;   // 没有硬件错误码时，stub 会手工补一个 0，保证布局统一。
  uint64_t rip;          // 返回用户态/内核态后，CPU 下一条要继续执行的指令地址。
  uint64_t cs;           // 当时的代码段选择子；最低两位还能看出 CPL。
  uint64_t rflags;       // 当时的 RFLAGS。
};

// 第一版 kernel-side TSS 初始化：
// - 安装一份内核自己的 GDT
// - 给 64 位 TSS 描述符分配槽位
// - 设置 RSP0 和 double-fault IST
// - 用 `ltr` 把 TSS 真正装进 task register
bool initialize_tss();
bool tss_is_ready();
uint16_t tss_task_register_selector();
uint64_t tss_kernel_rsp0();
uint64_t tss_default_kernel_rsp0();
bool tss_set_kernel_rsp0(uint64_t rsp0);
uint64_t tss_double_fault_ist1();
uint16_t tss_io_map_base();

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
static_assert(sizeof(RegisterInterruptFrame) == 160,
              "RegisterInterruptFrame layout must stay stable");

#endif
