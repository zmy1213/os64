#ifndef OS64_SYSCALL_HPP
#define OS64_SYSCALL_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/fd.hpp"

// 这一层叫 syscall，但现在还不是 CPU 的 `syscall` 指令。
// 它先定义“系统调用长什么样”：上层给路径或 fd，小整数/错误码返回。
enum SyscallStatus : int32_t {
  kSyscallOk = 0,                 // 调用成功。
  kSyscallInvalidArgument = -1,   // 参数本身不合法，比如空指针。
  kSyscallNotFound = -2,          // 路径不存在。
  kSyscallNotFile = -3,           // 路径存在，但不是普通文件。
  kSyscallBadFileDescriptor = -4, // fd 不存在或已经关闭。
  kSyscallIoError = -5,           // 底层读、关闭或 seek 失败。
};

// SyscallContext 是第一版“系统调用上下文”。
// 现在还没有进程，所以它只保存一张全局 fd 表；以后有进程后，这里会换成当前进程。
struct SyscallContext {
  FileDescriptorTable* fd_table;
};

bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table);
bool syscall_context_is_ready(const SyscallContext* context);

// 成功返回 fd，失败返回负数 SyscallStatus。
int32_t sys_open(SyscallContext* context, const char* path);

// 成功返回读到的字节数，EOF 返回 0，失败返回负数 SyscallStatus。
int32_t sys_read(SyscallContext* context, int32_t fd,
                 void* buffer, size_t bytes_to_read);

SyscallStatus sys_close(SyscallContext* context, int32_t fd);
SyscallStatus sys_seek(SyscallContext* context, int32_t fd, uint32_t offset);
SyscallStatus sys_stat(SyscallContext* context, int32_t fd,
                       VfsStat* out_stat);

#endif
