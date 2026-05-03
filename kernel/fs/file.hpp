#ifndef OS64_FILE_HPP
#define OS64_FILE_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/os64fs.hpp"

// FileStat 是给上层看的“文件元数据快照”。
// 它故意不暴露完整 Os64FsInode，避免 shell 以后和 OS64FS 的磁盘格式绑太死。
struct FileStat {
  uint32_t inode_number;      // 文件系统内部的 inode 编号，调试 stat 时很有用。
  uint16_t type;              // 当前对象类型：file 或 dir。
  uint16_t link_count;        // 当前 inode 的链接计数。
  uint32_t size_bytes;        // 文件内容大小，目录则是目录项区域大小。
  uint32_t direct_blocks[4];  // v1 仍然保留 direct block 信息，方便继续观察底层布局。
};

// FileHandle 是“已经打开的文件”。
// 这一层开始，上层读文件时只需要拿着句柄和 offset，不再直接操作 inode。
struct FileHandle {
  const Os64Fs* filesystem;  // 这个句柄属于哪个已挂载文件系统。
  Os64FsInode inode;         // 打开时把目标 inode 复制进来，后续 read 不再重复走路径查找。
  uint32_t offset;           // 下一次 read 应该从文件里的哪个字节开始读。
  bool open;                 // true 表示这个句柄当前可用。
};

// 打开普通文件。
// 目前第一版只读文件系统还没有目录句柄，所以打开目录会失败。
bool file_open(const Os64Fs* filesystem, const char* path,
               FileHandle* out_handle);

// 判断句柄是否真的处于打开状态。
bool file_is_open(const FileHandle* handle);

// 关闭句柄。
// 现在没有真正的资源回收，但先把接口立起来，后面接缓存/引用计数时不用改 shell。
bool file_close(FileHandle* handle);

// 通过路径拿元数据。
// 和 file_open 不同，file_stat 允许查看目录，因为 `stat docs` 这种行为本来就合理。
bool file_stat(const Os64Fs* filesystem, const char* path,
               FileStat* out_stat);

// 通过已经打开的句柄拿元数据。
bool file_handle_stat(const FileHandle* handle, FileStat* out_stat);

// 从当前 offset 开始读，成功读到多少字节就返回多少，并自动推进 offset。
// 到达 EOF 时返回 0。
size_t file_read(FileHandle* handle, void* buffer, size_t bytes_to_read);

// 调整下一次 read 的位置。
// 第一版先不允许 seek 到文件末尾之后，避免新手调试时出现“看似成功但永远读不到”的状态。
bool file_seek(FileHandle* handle, uint32_t offset);

// 查看当前 read offset。
uint32_t file_tell(const FileHandle* handle);

#endif
