#ifndef OS64_FD_HPP
#define OS64_FD_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/vfs.hpp"

constexpr int32_t kInvalidFileDescriptor = -1;
constexpr size_t kFileDescriptorCapacity = 16;

// FileDescriptorEntry 是 fd 表里的一个槽位。
// 真实系统里用户进程看到的 fd 只是 0、1、2 这种小整数；
// 内核会用这个整数去表里找到真正打开的文件对象。
struct FileDescriptorEntry {
  VfsFile file;  // 这个槽位背后真正打开的 VFS 文件。
  bool open;     // true 表示这个槽位已经被 fd_open 占用。
};

// FileDescriptorTable 是第一版“打开文件表”。
// 现在还没有进程，所以先做成一张全局表；以后有进程后，每个进程会有自己的 fd 表。
struct FileDescriptorTable {
  const VfsMount* vfs;  // fd_open 需要从哪个 VFS 挂载点打开路径。
  FileDescriptorEntry entries[kFileDescriptorCapacity];
  uint32_t open_count;  // 当前有多少个 fd 处于打开状态，方便测试和排错。
};

bool initialize_file_descriptor_table(FileDescriptorTable* table,
                                      const VfsMount* vfs);
bool file_descriptor_table_is_ready(const FileDescriptorTable* table);

int32_t fd_open(FileDescriptorTable* table, const char* path);
bool fd_is_open(const FileDescriptorTable* table, int32_t fd);
size_t fd_read(FileDescriptorTable* table, int32_t fd,
               void* buffer, size_t bytes_to_read);
bool fd_close(FileDescriptorTable* table, int32_t fd);
bool fd_stat(const FileDescriptorTable* table, int32_t fd,
             VfsStat* out_stat);
bool fd_seek(FileDescriptorTable* table, int32_t fd, uint32_t offset);
uint32_t fd_tell(const FileDescriptorTable* table, int32_t fd);
uint32_t fd_open_count(const FileDescriptorTable* table);

#endif
