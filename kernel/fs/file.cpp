#include "fs/file.hpp"

#include "runtime/runtime.hpp"

namespace {

// 把底层 inode 里的元数据提取成更稳定的 FileStat 形状。
// 这样 shell / VFS 之类的上层就不用直接依赖完整磁盘 inode 布局。
bool copy_inode_to_stat(const Os64FsInode* inode, FileStat* out_stat) {
  if (inode == nullptr || out_stat == nullptr) {
    return false;
  }

  out_stat->inode_number = inode->inode_number;
  out_stat->type = inode->type;
  out_stat->link_count = inode->link_count;
  out_stat->size_bytes = inode->size_bytes;
  out_stat->mode = inode->mode;
  out_stat->block_count = inode->block_count;
  out_stat->indirect_block = inode->indirect_block;

  for (size_t i = 0; i < kOs64FsDirectBlockCount; ++i) {
    out_stat->direct_blocks[i] = inode->direct_blocks[i];
  }

  return true;
}

}  // namespace

bool file_open(const Os64Fs* filesystem, const char* path,
               FileHandle* out_handle) {
  if (out_handle == nullptr) {
    return false;
  }

  // 不管最后能不能打开，先把输出句柄清零。
  memory_set(out_handle, 0, sizeof(*out_handle));

  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  // 先把路径解析成 inode；这里只有普通文件允许作为 FileHandle 打开。
  Os64FsInode inode;
  if (!os64fs_lookup_path(filesystem, path, &inode) ||
      inode.type != kOs64FsTypeFile) {
    return false;
  }

  // 打开成功后，把 inode 拷一份到句柄里。
  // 后续 read 就可以直接按这个 inode 读，不必每次重走路径解析。
  out_handle->filesystem = filesystem;
  memory_copy(&out_handle->inode, &inode, sizeof(inode));
  out_handle->offset = 0;
  out_handle->open = true;
  return true;
}

bool file_is_open(const FileHandle* handle) {
  return handle != nullptr &&
         handle->open &&
         os64fs_is_mounted(handle->filesystem) &&
         handle->inode.type == kOs64FsTypeFile;
}

bool file_close(FileHandle* handle) {
  if (handle == nullptr || !handle->open) {
    return false;
  }

  memory_set(handle, 0, sizeof(*handle));
  return true;
}

bool file_stat(const Os64Fs* filesystem, const char* path,
               FileStat* out_stat) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr ||
      out_stat == nullptr) {
    return false;
  }

  // stat 的特点是“只看元数据，不真的打开一个持续存在的句柄”。
  Os64FsInode inode;
  if (!os64fs_lookup_path(filesystem, path, &inode)) {
    return false;
  }

  return copy_inode_to_stat(&inode, out_stat);
}

bool file_handle_stat(const FileHandle* handle, FileStat* out_stat) {
  if (!file_is_open(handle)) {
    return false;
  }

  Os64FsInode inode;
  return os64fs_read_inode(handle->filesystem, handle->inode.inode_number, &inode) &&
         inode.type == kOs64FsTypeFile && copy_inode_to_stat(&inode, out_stat);
}

size_t file_read(FileHandle* handle, void* buffer, size_t bytes_to_read) {
  if (!file_is_open(handle) || buffer == nullptr || bytes_to_read == 0) {
    return 0;
  }

  // Another descriptor may have replaced or appended the file after open.
  // Refresh block references as well as size before reading any data.
  Os64FsInode current;
  if (!os64fs_read_inode(handle->filesystem, handle->inode.inode_number, &current) ||
      current.type != kOs64FsTypeFile) return 0;
  handle->inode = current;

  // 已经在文件末尾了，就直接返回 0，表示 EOF。
  if (handle->offset >= handle->inode.size_bytes) {
    return 0;
  }

  // 这一轮真正读多少字节，不能超过调用者想读的大小，也不能越过文件末尾。
  size_t bytes_this_read = bytes_to_read;
  const uint32_t remaining =
      handle->inode.size_bytes - handle->offset;
  if (bytes_this_read > remaining) {
    bytes_this_read = remaining;
  }

  // 具体怎么跨 direct/indirect block 读数据，由 OS64FS 层负责。
  if (!os64fs_read_inode_data(handle->filesystem, &handle->inode,
                              handle->offset, buffer, bytes_this_read)) {
    return 0;
  }

  // 成功后推进文件偏移，让下一次 read 从后面接着读。
  handle->offset += static_cast<uint32_t>(bytes_this_read);
  return bytes_this_read;
}

bool file_seek(FileHandle* handle, uint32_t offset) {
  if (!file_is_open(handle)) return false;
  Os64FsInode current;
  if (!os64fs_read_inode(handle->filesystem, handle->inode.inode_number, &current) ||
      current.type != kOs64FsTypeFile || offset > current.size_bytes) {
    return false;
  }

  handle->inode = current;

  handle->offset = offset;
  return true;
}

uint32_t file_tell(const FileHandle* handle) {
  if (!file_is_open(handle)) {
    return 0;
  }

  return handle->offset;
}
