#include "syscall/syscall.hpp"

#include "runtime/runtime.hpp"

namespace {

constexpr size_t kMaxSyscallPositiveResult = 2147483647U;

bool syscall_fd_is_open(SyscallContext* context, int32_t fd) {
  return syscall_context_is_ready(context) &&
         fd_is_open(context->fd_table, fd);
}

}  // namespace

bool initialize_syscall_context(SyscallContext* context,
                                FileDescriptorTable* fd_table) {
  if (context == nullptr) {
    return false;
  }

  // 先清空，避免初始化失败时留下半有效的旧 fd 表指针。
  memory_set(context, 0, sizeof(*context));

  if (!file_descriptor_table_is_ready(fd_table)) {
    return false;
  }

  context->fd_table = fd_table;
  return true;
}

bool syscall_context_is_ready(const SyscallContext* context) {
  return context != nullptr &&
         file_descriptor_table_is_ready(context->fd_table);
}

int32_t sys_open(SyscallContext* context, const char* path) {
  if (!syscall_context_is_ready(context) || path == nullptr) {
    return kSyscallInvalidArgument;
  }

  // open 前先 stat 一下，这样能把“不存在”和“路径是目录”分成不同错误码。
  VfsStat stat;
  if (!vfs_stat(context->fd_table->vfs, path, &stat)) {
    return kSyscallNotFound;
  }

  if (stat.type != kVfsNodeTypeFile) {
    return kSyscallNotFile;
  }

  const int32_t fd = fd_open(context->fd_table, path);
  if (fd == kInvalidFileDescriptor) {
    return kSyscallIoError;
  }

  return fd;
}

int32_t sys_read(SyscallContext* context, int32_t fd,
                 void* buffer, size_t bytes_to_read) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  if (bytes_to_read == 0) {
    return 0;
  }

  if (buffer == nullptr || bytes_to_read > kMaxSyscallPositiveResult) {
    return kSyscallInvalidArgument;
  }

  if (!fd_is_open(context->fd_table, fd)) {
    return kSyscallBadFileDescriptor;
  }

  const size_t bytes_read =
      fd_read(context->fd_table, fd, buffer, bytes_to_read);
  if (bytes_read > kMaxSyscallPositiveResult) {
    return kSyscallIoError;
  }

  return static_cast<int32_t>(bytes_read);
}

SyscallStatus sys_close(SyscallContext* context, int32_t fd) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  if (!syscall_fd_is_open(context, fd)) {
    return kSyscallBadFileDescriptor;
  }

  return fd_close(context->fd_table, fd) ? kSyscallOk : kSyscallIoError;
}

SyscallStatus sys_seek(SyscallContext* context, int32_t fd, uint32_t offset) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  if (!syscall_fd_is_open(context, fd)) {
    return kSyscallBadFileDescriptor;
  }

  return fd_seek(context->fd_table, fd, offset) ? kSyscallOk : kSyscallIoError;
}

SyscallStatus sys_stat(SyscallContext* context, int32_t fd,
                       VfsStat* out_stat) {
  if (!syscall_context_is_ready(context) || out_stat == nullptr) {
    return kSyscallInvalidArgument;
  }

  if (!syscall_fd_is_open(context, fd)) {
    return kSyscallBadFileDescriptor;
  }

  return fd_stat(context->fd_table, fd, out_stat) ? kSyscallOk
                                                  : kSyscallIoError;
}
