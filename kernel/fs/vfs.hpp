#ifndef OS64_VFS_HPP
#define OS64_VFS_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/directory.hpp"
#include "fs/file.hpp"

constexpr uint16_t kVfsNodeTypeFile = 1;       // VFS 上层看到的普通文件。
constexpr uint16_t kVfsNodeTypeDirectory = 2;  // VFS 上层看到的目录。
constexpr uint32_t kVfsInvalidBlockIndex = kOs64FsInvalidBlockIndex;
constexpr size_t kVfsDirectBlockCount = kOs64FsDirectBlockCount;
constexpr size_t kVfsDirectoryEntryNameCapacity = kDirectoryEntryNameCapacity;

// VfsMount 表示“一个已经挂载进 VFS 的文件系统”。
// 现在它只包一份 OS64FS；以后有多个文件系统时，这里会继续长出 ops 表。
struct VfsMount {
  Os64Fs* os64fs;        // 第一版 VFS 先只挂载一个 OS64FS 根文件系统；现在上层已经能发起写操作，所以这里不再只读。
  bool mounted;          // true 表示 VFS 入口已经可用。
};

// VfsStat 是 VFS 层统一给上层看的 stat 结果。
// shell 看到它以后，不需要知道底层到底是不是 OS64FS inode。
struct VfsStat {
  uint32_t inode_number;      // 当前底层还是 OS64FS inode 编号，先保留用于教学调试。
  uint16_t type;              // file 或 directory。
  uint16_t link_count;        // 链接计数。
  uint32_t size_bytes;        // 文件内容大小，目录则是目录项区域大小。
  uint32_t mode;              // 透传底层 mode，后面做权限模型时上层接口不用再变。
  uint32_t block_count;       // 这份文件/目录当前一共占用了多少个数据块。
  uint32_t indirect_block;    // 如果已经用到了单级间接块，这里把块号一起透给上层。
  uint32_t direct_blocks[kVfsDirectBlockCount];
                              // 保留 direct block 信息，方便 `stat` 继续观察布局。
};

// VfsFile 是 VFS 层的打开文件对象。
// 现在内部仍然转给 FileHandle；以后可以替换成 FileOps。
struct VfsFile {
  FileHandle handle;
};

// VfsDirectory 是 VFS 层的打开目录对象。
// 现在内部仍然转给 DirectoryHandle；以后可以替换成 DirectoryOps。
struct VfsDirectory {
  DirectoryHandle handle;
};

// VFS 层统一给上层看的目录项。
struct VfsDirectoryEntry {
  uint32_t inode_number;
  uint16_t type;
  uint8_t name_length;
  char name[kVfsDirectoryEntryNameCapacity + 1];
  uint32_t size_bytes;
};

// 用一个已经挂载好的 OS64FS 初始化 VFS 入口。
// 这一版 VFS 还很薄，但它已经开始承担“统一上层接口、隔离底层文件系统格式”的作用。
bool initialize_vfs(VfsMount* mount, Os64Fs* filesystem);
// 判断 VFS 根挂载是否可用。
bool vfs_is_mounted(const VfsMount* mount);
// 把统一 VFS 节点类型翻译成文本，给 shell `stat` / `ls` 之类的输出使用。
const char* vfs_node_type_name(uint16_t type);

// 按路径拿一个统一的 stat 结果。
// 上层只看 VfsStat，不需要知道底下其实是 Os64FsInode。
bool vfs_stat(const VfsMount* mount, const char* path, VfsStat* out_stat);

// 打开普通文件。
// 成功后 `out_file` 里会保存一个 VFS 层文件句柄。
bool vfs_open_file(const VfsMount* mount, const char* path,
                   VfsFile* out_file);
// 判断文件句柄是否可用。
bool vfs_file_is_open(const VfsFile* file);
// 关闭文件句柄。
bool vfs_close_file(VfsFile* file);
// 读取已经打开文件的元数据。
bool vfs_file_stat(const VfsFile* file, VfsStat* out_stat);
// 从当前 offset 开始读文件，并推进 offset。
size_t vfs_read_file(VfsFile* file, void* buffer, size_t bytes_to_read);
// 调整文件读指针。
bool vfs_seek_file(VfsFile* file, uint32_t offset);
// 返回当前文件读指针位置。
uint32_t vfs_tell_file(const VfsFile* file);

// 打开目录。
bool vfs_open_directory(const VfsMount* mount, const char* path,
                        VfsDirectory* out_directory);
// 判断目录句柄是否可用。
bool vfs_directory_is_open(const VfsDirectory* directory);
// 关闭目录句柄。
bool vfs_close_directory(VfsDirectory* directory);
// 返回目录项总数。
uint32_t vfs_directory_entry_count(const VfsDirectory* directory);
// 顺序读取下一条目录项。
bool vfs_read_directory(VfsDirectory* directory,
                        VfsDirectoryEntry* out_entry);
// 把目录读取位置重置回开头。
bool vfs_rewind_directory(VfsDirectory* directory);
// 返回当前目录读取游标。
uint32_t vfs_tell_directory(const VfsDirectory* directory);
// 下面这些接口就是 VFS 暴露给更上层的“写操作”入口。
// 当前底层仍然只有一个 OS64FS，但 shell / syscall / fd 层以后都可以只认 VFS。
bool vfs_create_file(VfsMount* mount, const char* path);
bool vfs_create_directory(VfsMount* mount, const char* path);
bool vfs_write_file(VfsMount* mount, const char* path,
                    const void* buffer, size_t bytes_to_write);
bool vfs_append_file(VfsMount* mount, const char* path,
                     const void* buffer, size_t bytes_to_write);
bool vfs_unlink(VfsMount* mount, const char* path);
bool vfs_sync(VfsMount* mount);

#endif
