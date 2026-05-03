#ifndef OS64_SYSCALL_HPP
#define OS64_SYSCALL_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/fd.hpp"

constexpr size_t kSyscallPathCapacity = 64;  // 第一版每个“进程上下文”的 cwd 先限制在 63 个字符内。

// 这一层叫 syscall，但现在还不是 CPU 的 `syscall` 指令。
// 它先定义“系统调用长什么样”：上层给路径或 fd，小整数/错误码返回。
enum SyscallStatus : int32_t {
  kSyscallOk = 0,                 // 调用成功。
  kSyscallInvalidArgument = -1,   // 参数本身不合法，比如空指针。
  kSyscallNotFound = -2,          // 路径不存在。
  kSyscallNotFile = -3,           // 路径存在，但类型不符合这次调用要求。
  kSyscallBadFileDescriptor = -4, // fd 不存在或已经关闭。
  kSyscallIoError = -5,           // 底层读、关闭或 seek 失败。
};

// SyscallContext 是第一版“系统调用上下文”。
// 现在还没有进程，所以它先保存两样最关键的状态：
// 1. 当前这组调用共用哪张 fd 表
// 2. 当前工作目录 cwd 是什么
// 以后有进程后，这里会自然长成“当前进程的内核视图”。
struct SyscallContext {
  FileDescriptorTable* fd_table;                    // 以后 open/read/close 都会先从这里进入 fd 层。
  char current_working_directory[kSyscallPathCapacity];  // 现在先把 cwd 放进 syscall 上下文，而不是留在 shell 私有状态里。
};

bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table);
bool syscall_context_is_ready(const SyscallContext* context);
const char* syscall_current_working_directory(const SyscallContext* context);

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

#endif
