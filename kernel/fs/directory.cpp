#include "fs/directory.hpp"

#include "runtime/runtime.hpp"

namespace {

bool copy_dir_entry_to_public_entry(const Os64FsDirEntry* source,
                                    const Os64FsInode* child_inode,
                                    DirectoryEntry* out_entry) {
  if (source == nullptr || child_inode == nullptr || out_entry == nullptr ||
      source->name_length > kDirectoryEntryNameCapacity) {
    return false;
  }

  memory_set(out_entry, 0, sizeof(*out_entry));
  out_entry->inode_number = source->inode_number;
  out_entry->type = source->type;
  out_entry->name_length = source->name_length;
  out_entry->size_bytes = child_inode->size_bytes;

  for (size_t i = 0; i < source->name_length; ++i) {
    out_entry->name[i] = source->name[i];
  }
  out_entry->name[source->name_length] = '\0';

  return true;
}

}  // namespace

bool directory_open(const Os64Fs* filesystem, const char* path,
                    DirectoryHandle* out_handle) {
  if (out_handle == nullptr) {
    return false;
  }

  memory_set(out_handle, 0, sizeof(*out_handle));

  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  Os64FsInode inode;
  if (!os64fs_lookup_path(filesystem, path, &inode) ||
      inode.type != kOs64FsTypeDirectory) {
    return false;
  }

  out_handle->filesystem = filesystem;
  out_handle->inode = inode;
  out_handle->next_entry_index = 0;
  out_handle->entry_count =
      os64fs_directory_entry_count(filesystem, &inode);
  out_handle->open = true;
  return true;
}

bool directory_is_open(const DirectoryHandle* handle) {
  return handle != nullptr &&
         handle->open &&
         os64fs_is_mounted(handle->filesystem) &&
         handle->inode.type == kOs64FsTypeDirectory;
}

bool directory_close(DirectoryHandle* handle) {
  if (!directory_is_open(handle)) {
    return false;
  }

  memory_set(handle, 0, sizeof(*handle));
  return true;
}

uint32_t directory_entry_count(const DirectoryHandle* handle) {
  if (!directory_is_open(handle)) {
    return 0;
  }

  return handle->entry_count;
}

bool directory_read(DirectoryHandle* handle, DirectoryEntry* out_entry) {
  if (!directory_is_open(handle) || out_entry == nullptr ||
      handle->next_entry_index >= handle->entry_count) {
    return false;
  }

  Os64FsDirEntry raw_entry;
  Os64FsInode child_inode;
  if (!os64fs_read_directory_entry(handle->filesystem, &handle->inode,
                                   handle->next_entry_index, &raw_entry) ||
      !os64fs_read_inode(handle->filesystem, raw_entry.inode_number,
                         &child_inode) ||
      !copy_dir_entry_to_public_entry(&raw_entry, &child_inode, out_entry)) {
    return false;
  }

  ++handle->next_entry_index;
  return true;
}

bool directory_rewind(DirectoryHandle* handle) {
  if (!directory_is_open(handle)) {
    return false;
  }

  handle->next_entry_index = 0;
  return true;
}

uint32_t directory_tell(const DirectoryHandle* handle) {
  if (!directory_is_open(handle)) {
    return 0;
  }

  return handle->next_entry_index;
}
