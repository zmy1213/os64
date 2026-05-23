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
// 判断 fd 表是否已经绑定到一个可用的 VFS 挂载点上。
bool file_descriptor_table_is_ready(const FileDescriptorTable* table);

// 打开路径并返回一个小整数 fd。
// 成功时返回 `0..kFileDescriptorCapacity-1`；
// 失败时返回 `kInvalidFileDescriptor`。
int32_t fd_open(FileDescriptorTable* table, const char* path);
// 判断某个小整数 fd 现在是否真的对应一个打开文件。
bool fd_is_open(const FileDescriptorTable* table, int32_t fd);
// 通过 fd 读取文件内容。
// 这一步会继续复用 VFS/FileHandle 层维护的当前 offset。
size_t fd_read(FileDescriptorTable* table, int32_t fd,
               void* buffer, size_t bytes_to_read);
// 关闭 fd，并释放这个槽位。
bool fd_close(FileDescriptorTable* table, int32_t fd);
// 通过 fd 拿元数据。
bool fd_stat(const FileDescriptorTable* table, int32_t fd,
             VfsStat* out_stat);
// 调整 fd 当前读指针。
bool fd_seek(FileDescriptorTable* table, int32_t fd, uint32_t offset);
// 查询 fd 当前读指针。
uint32_t fd_tell(const FileDescriptorTable* table, int32_t fd);
// 统计当前表里有多少个 fd 正处于打开状态。
uint32_t fd_open_count(const FileDescriptorTable* table);

#endif
