#ifndef OS64_FD_HPP
#define OS64_FD_HPP
#include <stddef.h>
#include <stdint.h>
#include "fs/vfs.hpp"
constexpr int32_t kInvalidFileDescriptor=-1;
// 普通文件槽位仍是内部 0..15，对用户公开为 3..18，兼容早期教程。
constexpr size_t kFileDescriptorCapacity=16;
constexpr size_t kFileDescriptorSlotCapacity=kFileDescriptorCapacity+3;
constexpr size_t kPublicFileDescriptorCapacity=kFileDescriptorSlotCapacity;
constexpr size_t kFileDescriptorPathCapacity=64;
constexpr size_t kPipeBufferBytes=4096;
constexpr int32_t kFdBrokenPipe=-7;
enum FileOpenFlags : uint32_t {
  kOpenRead=1, kOpenWrite=2, kOpenCreate=4, kOpenTruncate=8, kOpenAppend=16,
};
enum FileDescriptorKind : uint8_t {
  kDescriptorInvalid=0, kDescriptorFile, kDescriptorPipeRead, kDescriptorPipeWrite,
  kDescriptorTerminalInput, kDescriptorTerminalOutput,
};
struct PipeState;
// fd 只是每个进程的“标签”；真正的文件位置/管道端点在共享对象里。
// dup 和 spawn 只增加 references，所以两个标签读取同一文件时共享 offset。
struct OpenFileDescription {
  FileDescriptorKind kind;
  uint32_t references;
  VfsFile file;
  uint32_t flags;
  char path[kFileDescriptorPathCapacity];
  PipeState* pipe;
  int32_t terminal_number;
};
struct FileDescriptorEntry {
  OpenFileDescription* description;
  bool open;
};
struct FileDescriptorTable {
  const VfsMount* vfs;
  // 最后三槽对应公开 0/1/2。未重定向时隐式绑定终端，不额外分配对象。
  FileDescriptorEntry entries[kFileDescriptorSlotCapacity];
  bool standard_closed[3];
  uint32_t open_count;  // 只计显式持有的引用，包括重定向后的标准流。
};
bool initialize_file_descriptor_table(FileDescriptorTable*, const VfsMount*);
bool file_descriptor_table_is_ready(const FileDescriptorTable*);
int32_t fd_public_to_slot(int32_t public_fd);
int32_t fd_slot_to_public(int32_t slot);
int32_t fd_open(FileDescriptorTable*, const char*, uint32_t flags=kOpenRead);
bool fd_is_open(const FileDescriptorTable*, int32_t);
FileDescriptorKind fd_kind(const FileDescriptorTable*, int32_t);
int32_t fd_terminal_number(const FileDescriptorTable*, int32_t);
int32_t fd_read(FileDescriptorTable*, int32_t, void*, size_t);
bool fd_can_read(const FileDescriptorTable*, int32_t);
bool fd_can_write(const FileDescriptorTable*, int32_t);
int32_t fd_write(FileDescriptorTable*, int32_t, const void*, size_t);
bool fd_close(FileDescriptorTable*, int32_t);
void fd_close_all(FileDescriptorTable*);
void fd_close_nonstandard(FileDescriptorTable*);
bool fd_inherit(FileDescriptorTable* destination, const FileDescriptorTable* source);
// 下列函数接收内部槽号；syscall 层负责公开 fd 与槽号之间的翻译。
int32_t fd_dup2(FileDescriptorTable*, int32_t old_slot, int32_t new_slot);
int32_t fd_dup(FileDescriptorTable*, int32_t old_slot);
bool fd_pipe(FileDescriptorTable*, int32_t slots[2]);
bool fd_references_inode(const FileDescriptorTable*, uint32_t inode_number);
bool fd_stat(const FileDescriptorTable*, int32_t, VfsStat*);
bool fd_seek(FileDescriptorTable*, int32_t, uint32_t);
uint32_t fd_tell(const FileDescriptorTable*, int32_t);
uint32_t fd_open_count(const FileDescriptorTable*);
#endif
