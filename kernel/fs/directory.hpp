#ifndef OS64_DIRECTORY_HPP
#define OS64_DIRECTORY_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/os64fs.hpp"

constexpr size_t kDirectoryEntryNameCapacity = kOs64FsDirectoryEntryNameCapacity;
                                                    // 目录句柄层直接跟随底层目录项名字容量一起升级。

// DirectoryEntry 是给上层看的目录项。
// 它把底层 Os64FsDirEntry 包了一层，并顺手带上目标 inode 的大小，方便 `ls` 直接展示。
struct DirectoryEntry {
  uint32_t inode_number;                         // 这个名字指向哪个 inode。
  uint16_t type;                                 // 这个名字对应 file 还是 dir。
  uint8_t name_length;                           // 名字实际有多少字节。
  char name[kDirectoryEntryNameCapacity + 1];    // 多留 1 字节放 '\0'，方便以后做字符串输出。
  uint32_t size_bytes;                           // 目标 inode 的大小；文件是内容大小，目录是目录项区域大小。
};

// DirectoryHandle 表示“已经打开的目录”。
// 它和 FileHandle 的意义类似，只是 read 的单位从“字节”变成“目录项”。
struct DirectoryHandle {
  const Os64Fs* filesystem;  // 这个目录句柄属于哪个已挂载文件系统。
  Os64FsInode inode;         // 打开目录时找到的目录 inode 副本。
  uint32_t next_entry_index; // 下一次 directory_read 应该读取第几个目录项。
  uint32_t entry_count;      // 当前目录一共有多少个目录项。
  bool open;                 // true 表示这个目录句柄当前有效。
};

// 打开目录。
// 第一版目录句柄只处理目录，不处理普通文件。
bool directory_open(const Os64Fs* filesystem, const char* path,
                    DirectoryHandle* out_handle);

// 判断目录句柄是否可用。
bool directory_is_open(const DirectoryHandle* handle);

// 关闭目录句柄。
bool directory_close(DirectoryHandle* handle);

// 返回当前目录总共有多少个目录项。
uint32_t directory_entry_count(const DirectoryHandle* handle);

// 顺序读取一个目录项。
// 成功读到一项返回 true；如果已经读到目录末尾，返回 false。
bool directory_read(DirectoryHandle* handle, DirectoryEntry* out_entry);

// 把目录句柄重新移动到第 0 个目录项。
bool directory_rewind(DirectoryHandle* handle);

// 返回下一次 directory_read 会读取的目录项下标。
uint32_t directory_tell(const DirectoryHandle* handle);

#endif
