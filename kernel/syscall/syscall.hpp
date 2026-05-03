#ifndef OS64_SYSCALL_HPP
#define OS64_SYSCALL_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/fd.hpp"

constexpr size_t kSyscallPathCapacity = 64;  // 第一版每个“进程上下文”的 cwd 先限制在 63 个字符内。
constexpr int32_t kSyscallStandardInputFd = 0;    // 公开 syscall fd 里，0 预留给标准输入。
constexpr int32_t kSyscallStandardOutputFd = 1;   // 公开 syscall fd 里，1 预留给标准输出。
constexpr int32_t kSyscallStandardErrorFd = 2;    // 公开 syscall fd 里，2 预留给标准错误。
constexpr int32_t kSyscallFirstFileFd = 3;        // 3 以后才映射到内核内部真正的“打开文件表”。

// 这一层叫 syscall，但现在还不是 CPU 的 `syscall` 指令。
// 它先定义“系统调用长什么样”：上层给路径或 fd，小整数/错误码返回。
enum SyscallStatus : int32_t {
  kSyscallOk = 0,                 // 调用成功。
  kSyscallInvalidArgument = -1,   // 参数本身不合法，比如空指针。
  kSyscallNotFound = -2,          // 路径不存在。
  kSyscallNotFile = -3,           // 路径存在，但类型不符合这次调用要求。
  kSyscallBadFileDescriptor = -4, // fd 不存在或已经关闭。
  kSyscallIoError = -5,           // 底层读、关闭或 seek 失败。
  kSyscallUnsupported = -6,       // 这条 syscall 形状已经有了，但当前内核版本还没支持这类对象/操作。
};

// 这是第一版“寄存器 ABI”。
// 当前我们先用 `int 0x80` 软中断把寄存器带进内核，再转调现有 `sys_*` C++ 接口。
// 约定先尽量简单，顺序也故意贴近 SysV 常见寄存器名字：
// - RAX = syscall 编号
// - RDI = 第 1 个参数
// - RSI = 第 2 个参数
// - RDX = 第 3 个参数
// - RCX = 第 4 个参数
enum SyscallNumber : uint64_t {
  kSyscallNumberGetCwd = 0,
  kSyscallNumberChdir = 1,
  kSyscallNumberOpen = 2,
  kSyscallNumberRead = 3,
  kSyscallNumberClose = 4,
  kSyscallNumberSeek = 5,
  kSyscallNumberStat = 6,
  kSyscallNumberStatPath = 7,
  kSyscallNumberListDir = 8,
  kSyscallNumberWrite = 9,  // 这次先追加，不去重排前一轮已经用起来的编号。
};

// 这是第一版“写输出”回调。
// 当前内核还没有真正的用户态终端设备，所以先让 syscall 层把 stdout/stderr
// 交给外面提供的一个安全小回调，后面再慢慢替换成更正式的 TTY/设备层。
using SyscallWriteHandler = size_t (*)(int32_t fd,
                                       const void* buffer,
                                       size_t bytes_to_write,
                                       void* context);

// SyscallContext 是第一版“系统调用上下文”。
// 现在还没有进程，所以它先保存两样最关键的状态：
// 1. 当前这组调用共用哪张 fd 表
// 2. 当前工作目录 cwd 是什么
// 以后有进程后，这里会自然长成“当前进程的内核视图”。
struct SyscallContext {
  FileDescriptorTable* fd_table;                    // 以后 open/read/close 都会先从这里进入 fd 层。
  char current_working_directory[kSyscallPathCapacity];  // 现在先把 cwd 放进 syscall 上下文，而不是留在 shell 私有状态里。
  SyscallWriteHandler write_handler;                // stdout/stderr 现在先通过一个外部注入的最小回调往控制台写。
  void* write_context;                              // 给 write_handler 留一个不透明上下文指针，方便以后接设备对象。
};

// 这个结构必须严格匹配 `interrupt_stubs.asm` 里 `syscall_interrupt_stub`
// 的压栈顺序。
// 现在 `int 0x80` 进入内核后，最顶层先保存的是通用寄存器，
// 这样 C++ 处理函数既能读参数，也能把返回值写回 `rax`。
struct SyscallInterruptFrame {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rax;
  uint64_t vector;
  uint64_t error_code;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
};

bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table);
bool syscall_context_is_ready(const SyscallContext* context);
const char* syscall_current_working_directory(const SyscallContext* context);
bool install_syscall_write_handler(SyscallContext* context,
                                   SyscallWriteHandler handler,
                                   void* write_context);

// 现在还没有进程切换，所以 CPU 真正打进来的 syscall 统一先看“当前激活的上下文”。
// 以后这里自然会升级成“当前线程/当前进程的上下文”。
bool install_syscall_dispatch_context(SyscallContext* context);
bool syscall_dispatch_is_ready();

// 这是给内核内部模块用的辅助函数。
// shell 想打印 `cat_resolved_path=/docs/guide.txt` 这种日志时，可以复用同一套解析规则。
bool syscall_resolve_path(const SyscallContext* context,
                          const char* raw_path,
                          char* out_path,
                          size_t capacity);

// 成功返回当前 cwd 的长度，失败返回负数 SyscallStatus。
int32_t sys_getcwd(SyscallContext* context, char* buffer, size_t capacity);

// 成功返回 kSyscallOk，失败返回负数 SyscallStatus。
SyscallStatus sys_chdir(SyscallContext* context, const char* path);

// 成功返回 fd，失败返回负数 SyscallStatus。
int32_t sys_open(SyscallContext* context, const char* path);

// 成功返回读到的字节数，EOF 返回 0，失败返回负数 SyscallStatus。
int32_t sys_read(SyscallContext* context, int32_t fd,
                 void* buffer, size_t bytes_to_read);

// 成功返回真正写出的字节数；当前第一版只支持 stdout/stderr。
int32_t sys_write(SyscallContext* context, int32_t fd,
                  const void* buffer, size_t bytes_to_write);

// 这是 path 版的 stat。
// 它和 `sys_stat(fd, ...)` 的区别是：
// - `sys_stat_path` 直接看一个路径
// - `sys_stat` 看一个已经打开的 fd
SyscallStatus sys_stat_path(SyscallContext* context, const char* path,
                            VfsStat* out_stat);

// 第一版目录读取先做成“按路径一次性列出目录项”。
// 成功返回目录项数量；失败返回负数 SyscallStatus。
// 如果 `out_entries == nullptr && entry_capacity == 0`，它只返回目录项数量，不实际拷贝目录项。
int32_t sys_listdir(SyscallContext* context, const char* path,
                    VfsDirectoryEntry* out_entries, size_t entry_capacity);

SyscallStatus sys_close(SyscallContext* context, int32_t fd);
SyscallStatus sys_seek(SyscallContext* context, int32_t fd, uint32_t offset);
SyscallStatus sys_stat(SyscallContext* context, int32_t fd,
                       VfsStat* out_stat);

extern "C" void kernel_handle_syscall(SyscallInterruptFrame* frame);

static_assert(sizeof(SyscallInterruptFrame) == 160,
              "SyscallInterruptFrame layout must match syscall_interrupt_stub");

#endif
