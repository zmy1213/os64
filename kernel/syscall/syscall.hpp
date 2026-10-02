#ifndef OS64_SYSCALL_HPP
#define OS64_SYSCALL_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/fd.hpp"
#include "interrupts/interrupts.hpp"

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
  kSyscallNumberExit = 10,  // 结束当前用户进程，保存退出码并唤醒等待者。
  kSyscallNumberYield = 11, // 用户线程主动让出 CPU，之后从原调用点继续。
  kSyscallNumberMkdir = 12,
  kSyscallNumberGetPid = 13,
  kSyscallNumberSleep = 14,
  kSyscallNumberSpawn = 15,
  kSyscallNumberWaitPid = 16,
  kSyscallNumberUnlink = 17,
  kSyscallNumberSync = 18,
  kSyscallNumberOpenFlags = 19,
  kSyscallNumberBrk = 20,
  kSyscallNumberReplaceFile = 21,
};

// 这是第一版“写输出”回调。
// 当前内核还没有真正的用户态终端设备，所以先让 syscall 层把 stdout/stderr
// 交给外面提供的一个安全小回调，后面再慢慢替换成更正式的 TTY/设备层。
using SyscallWriteHandler = size_t (*)(int32_t fd,
                                       const void* buffer,
                                       size_t bytes_to_write,
                                       void* context);

// 每个进程自己的系统调用视图：fd 表、当前目录和控制台输出回调。
// 内核启动自测也能显式安装一份上下文，复用同样的文件接口。
struct SyscallContext {
  FileDescriptorTable* fd_table;                    // 以后 open/read/close 都会先从这里进入 fd 层。
  char current_working_directory[kSyscallPathCapacity];  // 现在先把 cwd 放进 syscall 上下文，而不是留在 shell 私有状态里。
  SyscallWriteHandler write_handler;                // stdout/stderr 现在先通过一个外部注入的最小回调往控制台写。
  void* write_context;                              // 给 write_handler 留一个不透明上下文指针，方便以后接设备对象。
};

// syscall 入口现在直接复用“完整寄存器帧”这个通用结构。
// 好处是：
// 1. `int 0x80` 和硬件 IRQ 终于在数据布局上统一了
// 2. 后面做“用户态被 timer 抢占”时，不需要再维护第二套几乎一样的字段
using SyscallInterruptFrame = RegisterInterruptFrame;

// 初始化一份 syscall 上下文。
// 当前它最核心的职责只有两个：
// 1. 记住这组系统调用应该使用哪张 fd 表
// 2. 记住当前工作目录 cwd
bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table);
// 判断上下文是否真的可用。
bool syscall_context_is_ready(const SyscallContext* context);
// 只读返回当前 cwd 字符串。
const char* syscall_current_working_directory(const SyscallContext* context);
// 给 stdout/stderr 安装一个实际输出回调。
// 当前 shell / user program 打到 `write(1, ...)`、`write(2, ...)` 时，最终都会落到这里。
bool install_syscall_write_handler(SyscallContext* context,
                                   SyscallWriteHandler handler,
                                   void* write_context);

// 为启动自测安装后备上下文；调度器运行时优先取当前线程所属进程的上下文。
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
int32_t sys_open(SyscallContext* context, const char* path,
                 uint32_t flags = 0);

// 返回字节数，文件 EOF 返回 0，失败返回负数 SyscallStatus；正常 stdin 无字符时阻塞。
// 现在：
// - `fd == 0` 会走第一版 stdin 键盘字符流
// - `fd >= 3` 检查该文件句柄的读权限并读取文件
int32_t sys_read(SyscallContext* context, int32_t fd,
                 void* buffer, size_t bytes_to_read);

// 返回写出字节数；stdout/stderr 输出控制台，普通文件要求句柄有写权限。
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

// 下面这几个接口都是“fd 版操作”：
// `sys_open` 先把路径变成公开 fd，
// 后续 `close/seek/stat` 就都围绕这个 fd 小整数工作。
SyscallStatus sys_close(SyscallContext* context, int32_t fd);
SyscallStatus sys_seek(SyscallContext* context, int32_t fd, uint32_t offset);
SyscallStatus sys_stat(SyscallContext* context, int32_t fd,
                       VfsStat* out_stat);

struct SchedulerState;
struct PageAllocator;
struct Os64Fs;
bool install_syscall_process_services(SchedulerState* scheduler,
                                      PageAllocator* allocator,
                                      const Os64Fs* filesystem,
                                      VfsMount* vfs);
int32_t sys_spawn(SyscallContext* context, const char* path,
                  const char* const* argv, size_t argc);
int32_t sys_waitpid(int32_t pid, int32_t* status);
SyscallStatus sys_mkdir(SyscallContext* context, const char* path);
SyscallStatus sys_unlink(SyscallContext* context, const char* path);
SyscallStatus sys_sync(SyscallContext* context);
// 0 查询末尾；成功返回新末尾，失败返回原末尾（并非负错误码）。
uint64_t sys_brk(uint64_t requested_break);
int32_t sys_replace_file(SyscallContext* context, const char* path,
                         const void* buffer, size_t size);

// 这是 `int 0x80` 打进内核后的 C++ 总入口。
// 汇编 stub 会先把寄存器现场整理成 `SyscallInterruptFrame`，
// 然后再把控制权交给这里做真正分发。
extern "C" void kernel_handle_syscall(SyscallInterruptFrame* frame);

static_assert(sizeof(SyscallInterruptFrame) == 160,
              "SyscallInterruptFrame layout must match syscall_interrupt_stub");

#endif
