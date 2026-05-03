#include "syscall/syscall.hpp"

#include "runtime/runtime.hpp"

namespace {

constexpr size_t kMaxSyscallPositiveResult = 2147483647U;

bool is_space_char(char ch) {
  return ch == ' ' || ch == '\t';
}

bool is_path_separator(char ch) {
  return ch == '/';
}

const char* skip_spaces(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }

  while (is_space_char(*text)) {
    ++text;
  }

  return text;
}

size_t string_length(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

const char* trim_trailing_spaces(const char* begin, const char* end) {
  while (end > begin && is_space_char(end[-1])) {
    --end;
  }

  return end;
}

bool copy_string(char* destination, size_t capacity, const char* source) {
  if (destination == nullptr || source == nullptr || capacity == 0) {
    return false;
  }

  const size_t length = string_length(source);
  if (length >= capacity) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    destination[i] = source[i];
  }
  destination[length] = '\0';
  return true;
}

bool set_root_path(char* path, size_t capacity) {
  if (path == nullptr || capacity < 2) {
    return false;
  }

  path[0] = '/';
  path[1] = '\0';
  return true;
}

const char* skip_path_separators(const char* cursor, const char* end) {
  while (cursor < end && is_path_separator(*cursor)) {
    ++cursor;
  }

  return cursor;
}

size_t path_component_length(const char* begin, const char* end) {
  size_t length = 0;
  while ((begin + length) < end &&
         !is_path_separator(begin[length])) {
    ++length;
  }

  return length;
}

bool path_component_is_dot(const char* component, size_t length) {
  return component != nullptr && length == 1 && component[0] == '.';
}

bool path_component_is_dot_dot(const char* component, size_t length) {
  return component != nullptr &&
         length == 2 &&
         component[0] == '.' &&
         component[1] == '.';
}

bool append_path_component(char* path,
                           size_t capacity,
                           const char* component,
                           size_t component_length) {
  if (path == nullptr || component == nullptr || capacity == 0 ||
      component_length == 0) {
    return false;
  }

  size_t current_length = string_length(path);
  const bool path_is_root =
      current_length == 1 && path[0] == '/';
  const size_t slash_bytes = path_is_root ? 0 : 1;
  if (current_length + slash_bytes + component_length >= capacity) {
    return false;
  }

  if (!path_is_root) {
    path[current_length++] = '/';
  }

  for (size_t i = 0; i < component_length; ++i) {
    path[current_length + i] = component[i];
  }

  path[current_length + component_length] = '\0';
  return true;
}

void pop_path_component(char* path) {
  if (path == nullptr) {
    return;
  }

  const size_t length = string_length(path);
  if (length <= 1) {
    (void)set_root_path(path, kSyscallPathCapacity);
    return;
  }

  size_t index = length;
  while (index > 1 && path[index - 1] != '/') {
    --index;
  }

  if (index <= 1) {
    (void)set_root_path(path, kSyscallPathCapacity);
    return;
  }

  path[index - 1] = '\0';
}

bool syscall_fd_is_open(SyscallContext* context, int32_t fd) {
  return syscall_context_is_ready(context) &&
         fd_is_open(context->fd_table, fd);
}

SyscallStatus stat_path_internal(SyscallContext* context, const char* path,
                                 VfsStat* out_stat, char* resolved_path,
                                 size_t resolved_capacity) {
  if (!syscall_context_is_ready(context) || out_stat == nullptr) {
    return kSyscallInvalidArgument;
  }

  char resolved[kSyscallPathCapacity];
  char* path_buffer = resolved;
  size_t path_capacity = sizeof(resolved);

  if (resolved_path != nullptr) {
    if (resolved_capacity < 2) {
      return kSyscallInvalidArgument;
    }

    path_buffer = resolved_path;
    path_capacity = resolved_capacity;
  }

  if (!syscall_resolve_path(context, path, path_buffer, path_capacity)) {
    return kSyscallInvalidArgument;
  }

  return vfs_stat(context->fd_table->vfs, path_buffer, out_stat)
             ? kSyscallOk
             : kSyscallNotFound;
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
  return set_root_path(context->current_working_directory,
                       sizeof(context->current_working_directory));
}

bool syscall_context_is_ready(const SyscallContext* context) {
  return context != nullptr &&
         file_descriptor_table_is_ready(context->fd_table);
}

const char* syscall_current_working_directory(const SyscallContext* context) {
  if (!syscall_context_is_ready(context) ||
      context->current_working_directory[0] == '\0') {
    return nullptr;
  }

  return context->current_working_directory;
}

bool syscall_resolve_path(const SyscallContext* context,
                          const char* raw_path,
                          char* out_path,
                          size_t capacity) {
  if (!syscall_context_is_ready(context) || out_path == nullptr ||
      capacity < 2) {
    return false;
  }

  const char* base_path = syscall_current_working_directory(context);
  if (base_path == nullptr) {
    return false;
  }

  const char* begin = skip_spaces(raw_path);
  if (begin == nullptr || begin[0] == '\0') {
    return copy_string(out_path, capacity, base_path);
  }

  const char* end = begin + string_length(begin);
  end = trim_trailing_spaces(begin, end);
  if (end <= begin) {
    return copy_string(out_path, capacity, base_path);
  }

  const bool absolute = is_path_separator(begin[0]);
  if (absolute) {
    if (!set_root_path(out_path, capacity)) {
      return false;
    }
  } else if (!copy_string(out_path, capacity, base_path)) {
    return false;
  }

  const char* cursor =
      absolute ? skip_path_separators(begin, end) : begin;
  while (cursor < end) {
    const size_t component_length =
        path_component_length(cursor, end);
    if (component_length == 0) {
      cursor = skip_path_separators(cursor, end);
      continue;
    }

    if (path_component_is_dot(cursor, component_length)) {
      cursor = skip_path_separators(cursor + component_length, end);
      continue;
    }

    if (path_component_is_dot_dot(cursor, component_length)) {
      pop_path_component(out_path);
      cursor = skip_path_separators(cursor + component_length, end);
      continue;
    }

    if (!append_path_component(out_path, capacity, cursor,
                               component_length)) {
      return false;
    }

    cursor = skip_path_separators(cursor + component_length, end);
  }

  return true;
}

int32_t sys_getcwd(SyscallContext* context, char* buffer, size_t capacity) {
  if (!syscall_context_is_ready(context) || buffer == nullptr ||
      capacity == 0) {
    return kSyscallInvalidArgument;
  }

  const char* cwd = syscall_current_working_directory(context);
  if (cwd == nullptr || !copy_string(buffer, capacity, cwd)) {
    return kSyscallInvalidArgument;
  }

  return static_cast<int32_t>(string_length(buffer));
}

SyscallStatus sys_chdir(SyscallContext* context, const char* path) {
  if (!syscall_context_is_ready(context) || path == nullptr) {
    return kSyscallInvalidArgument;
  }

  char resolved[kSyscallPathCapacity];
  VfsStat stat;
  const SyscallStatus status =
      stat_path_internal(context, path, &stat, resolved, sizeof(resolved));
  if (status != kSyscallOk) {
    return status;
  }

  if (stat.type != kVfsNodeTypeDirectory) {
    return kSyscallNotFile;
  }

  return copy_string(context->current_working_directory,
                     sizeof(context->current_working_directory),
                     resolved)
             ? kSyscallOk
             : kSyscallInvalidArgument;
}

int32_t sys_open(SyscallContext* context, const char* path) {
  if (!syscall_context_is_ready(context) || path == nullptr) {
    return kSyscallInvalidArgument;
  }

  char resolved[kSyscallPathCapacity];
  if (!syscall_resolve_path(context, path, resolved, sizeof(resolved))) {
    return kSyscallInvalidArgument;
  }

  // open 前先 stat 一下，这样能把“不存在”和“路径是目录”分成不同错误码。
  VfsStat stat;
  if (!vfs_stat(context->fd_table->vfs, resolved, &stat)) {
    return kSyscallNotFound;
  }

  if (stat.type != kVfsNodeTypeFile) {
    return kSyscallNotFile;
  }

  const int32_t fd = fd_open(context->fd_table, resolved);
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

SyscallStatus sys_stat_path(SyscallContext* context, const char* path,
                            VfsStat* out_stat) {
  return stat_path_internal(context, path, out_stat, nullptr, 0);
}

int32_t sys_listdir(SyscallContext* context, const char* path,
                    VfsDirectoryEntry* out_entries, size_t entry_capacity) {
  if (!syscall_context_is_ready(context)) {
    return kSyscallInvalidArgument;
  }

  char resolved[kSyscallPathCapacity];
  VfsStat stat;
  const SyscallStatus status =
      stat_path_internal(context, path, &stat, resolved, sizeof(resolved));
  if (status != kSyscallOk) {
    return status;
  }

  if (stat.type != kVfsNodeTypeDirectory) {
    return kSyscallNotFile;
  }

  VfsDirectory directory;
  if (!vfs_open_directory(context->fd_table->vfs, resolved, &directory)) {
    return kSyscallIoError;
  }

  const uint32_t entry_count =
      vfs_directory_entry_count(&directory);

  if (out_entries == nullptr && entry_capacity == 0) {
    (void)vfs_close_directory(&directory);
    return static_cast<int32_t>(entry_count);
  }

  if (out_entries == nullptr ||
      entry_capacity < static_cast<size_t>(entry_count)) {
    (void)vfs_close_directory(&directory);
    return kSyscallInvalidArgument;
  }

  for (uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
    if (!vfs_read_directory(&directory,
                            &out_entries[static_cast<size_t>(entry_index)])) {
      (void)vfs_close_directory(&directory);
      return kSyscallIoError;
    }
  }

  if (!vfs_close_directory(&directory)) {
    return kSyscallIoError;
  }

  return static_cast<int32_t>(entry_count);
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
