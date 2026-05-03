#ifndef OS64_VFS_HPP
#define OS64_VFS_HPP

#include <stddef.h>
#include <stdint.h>

#include "fs/directory.hpp"
#include "fs/file.hpp"

constexpr uint16_t kVfsNodeTypeFile = 1;       // VFS 上层看到的普通文件。
constexpr uint16_t kVfsNodeTypeDirectory = 2;  // VFS 上层看到的目录。
constexpr uint32_t kVfsInvalidBlockIndex = kOs64FsInvalidBlockIndex;
constexpr size_t kVfsDirectoryEntryNameCapacity = kDirectoryEntryNameCapacity;

// VfsMount 表示“一个已经挂载进 VFS 的文件系统”。
// 现在它只包一份 OS64FS；以后有多个文件系统时，这里会继续长出 ops 表。
struct VfsMount {
  const Os64Fs* os64fs;  // 第一版 VFS 先只挂载一个 OS64FS 根文件系统。
  bool mounted;          // true 表示 VFS 入口已经可用。
};

// VfsStat 是 VFS 层统一给上层看的 stat 结果。
// shell 看到它以后，不需要知道底层到底是不是 OS64FS inode。
struct VfsStat {
  uint32_t inode_number;      // 当前底层还是 OS64FS inode 编号，先保留用于教学调试。
  uint16_t type;              // file 或 directory。
  uint16_t link_count;        // 链接计数。
  uint32_t size_bytes;        // 文件内容大小，目录则是目录项区域大小。
  uint32_t direct_blocks[4];  // 先保留底层 direct block 信息，方便 `stat` 继续观察布局。
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

bool initialize_vfs(VfsMount* mount, const Os64Fs* filesystem);
bool vfs_is_mounted(const VfsMount* mount);
const char* vfs_node_type_name(uint16_t type);

bool vfs_stat(const VfsMount* mount, const char* path, VfsStat* out_stat);

bool vfs_open_file(const VfsMount* mount, const char* path,
                   VfsFile* out_file);
bool vfs_file_is_open(const VfsFile* file);
bool vfs_close_file(VfsFile* file);
bool vfs_file_stat(const VfsFile* file, VfsStat* out_stat);
size_t vfs_read_file(VfsFile* file, void* buffer, size_t bytes_to_read);
bool vfs_seek_file(VfsFile* file, uint32_t offset);
uint32_t vfs_tell_file(const VfsFile* file);

bool vfs_open_directory(const VfsMount* mount, const char* path,
                        VfsDirectory* out_directory);
bool vfs_directory_is_open(const VfsDirectory* directory);
bool vfs_close_directory(VfsDirectory* directory);
uint32_t vfs_directory_entry_count(const VfsDirectory* directory);
bool vfs_read_directory(VfsDirectory* directory,
                        VfsDirectoryEntry* out_entry);
bool vfs_rewind_directory(VfsDirectory* directory);
uint32_t vfs_tell_directory(const VfsDirectory* directory);

#endif
