#include "fs/file.hpp"

#include "runtime/runtime.hpp"

namespace {

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

  memory_set(out_handle, 0, sizeof(*out_handle));

  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  Os64FsInode inode;
  if (!os64fs_lookup_path(filesystem, path, &inode) ||
      inode.type != kOs64FsTypeFile) {
    return false;
  }

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
  if (!file_is_open(handle)) {
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

  return copy_inode_to_stat(&handle->inode, out_stat);
}

size_t file_read(FileHandle* handle, void* buffer, size_t bytes_to_read) {
  if (!file_is_open(handle) || buffer == nullptr || bytes_to_read == 0) {
    return 0;
  }

  if (handle->offset >= handle->inode.size_bytes) {
    return 0;
  }

  size_t bytes_this_read = bytes_to_read;
  const uint32_t remaining =
      handle->inode.size_bytes - handle->offset;
  if (bytes_this_read > remaining) {
    bytes_this_read = remaining;
  }

  if (!os64fs_read_inode_data(handle->filesystem, &handle->inode,
                              handle->offset, buffer, bytes_this_read)) {
    return 0;
  }

  handle->offset += static_cast<uint32_t>(bytes_this_read);
  return bytes_this_read;
}

bool file_seek(FileHandle* handle, uint32_t offset) {
  if (!file_is_open(handle) || offset > handle->inode.size_bytes) {
    return false;
  }

  handle->offset = offset;
  return true;
}

uint32_t file_tell(const FileHandle* handle) {
  if (!file_is_open(handle)) {
    return 0;
  }

  return handle->offset;
}
