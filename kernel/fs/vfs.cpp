#include "fs/vfs.hpp"

#include "runtime/runtime.hpp"

namespace {

uint16_t to_vfs_node_type(uint16_t type) {
  switch (type) {
    case kOs64FsTypeFile:
      return kVfsNodeTypeFile;
    case kOs64FsTypeDirectory:
      return kVfsNodeTypeDirectory;
    default:
      return 0;
  }
}

bool copy_file_stat_to_vfs_stat(const FileStat* source, VfsStat* out_stat) {
  if (source == nullptr || out_stat == nullptr) {
    return false;
  }

  memory_set(out_stat, 0, sizeof(*out_stat));
  out_stat->inode_number = source->inode_number;
  out_stat->type = to_vfs_node_type(source->type);
  out_stat->link_count = source->link_count;
  out_stat->size_bytes = source->size_bytes;

  for (size_t i = 0; i < 4; ++i) {
    out_stat->direct_blocks[i] = source->direct_blocks[i];
  }

  return out_stat->type == kVfsNodeTypeFile ||
         out_stat->type == kVfsNodeTypeDirectory;
}

bool copy_directory_entry_to_vfs_entry(const DirectoryEntry* source,
                                       VfsDirectoryEntry* out_entry) {
  if (source == nullptr || out_entry == nullptr ||
      source->name_length > kVfsDirectoryEntryNameCapacity) {
    return false;
  }

  memory_set(out_entry, 0, sizeof(*out_entry));
  out_entry->inode_number = source->inode_number;
  out_entry->type = to_vfs_node_type(source->type);
  out_entry->name_length = source->name_length;
  out_entry->size_bytes = source->size_bytes;

  for (size_t i = 0; i < source->name_length; ++i) {
    out_entry->name[i] = source->name[i];
  }
  out_entry->name[source->name_length] = '\0';

  return out_entry->type == kVfsNodeTypeFile ||
         out_entry->type == kVfsNodeTypeDirectory;
}

}  // namespace

bool initialize_vfs(VfsMount* mount, const Os64Fs* filesystem) {
  if (mount == nullptr) {
    return false;
  }

  memory_set(mount, 0, sizeof(*mount));

  if (!os64fs_is_mounted(filesystem)) {
    return false;
  }

  mount->os64fs = filesystem;
  mount->mounted = true;
  return true;
}

bool vfs_is_mounted(const VfsMount* mount) {
  return mount != nullptr &&
         mount->mounted &&
         os64fs_is_mounted(mount->os64fs);
}

const char* vfs_node_type_name(uint16_t type) {
  switch (type) {
    case kVfsNodeTypeFile:
      return "file";
    case kVfsNodeTypeDirectory:
      return "dir";
    default:
      return "unknown";
  }
}

bool vfs_stat(const VfsMount* mount, const char* path, VfsStat* out_stat) {
  if (!vfs_is_mounted(mount) || path == nullptr || out_stat == nullptr) {
    return false;
  }

  FileStat file_stat_result;
  if (!file_stat(mount->os64fs, path, &file_stat_result)) {
    return false;
  }

  return copy_file_stat_to_vfs_stat(&file_stat_result, out_stat);
}

bool vfs_open_file(const VfsMount* mount, const char* path,
                   VfsFile* out_file) {
  if (out_file == nullptr) {
    return false;
  }

  memory_set(out_file, 0, sizeof(*out_file));

  return vfs_is_mounted(mount) &&
         file_open(mount->os64fs, path, &out_file->handle);
}

bool vfs_file_is_open(const VfsFile* file) {
  return file != nullptr && file_is_open(&file->handle);
}

bool vfs_close_file(VfsFile* file) {
  if (!vfs_file_is_open(file)) {
    return false;
  }

  return file_close(&file->handle);
}

bool vfs_file_stat(const VfsFile* file, VfsStat* out_stat) {
  if (!vfs_file_is_open(file) || out_stat == nullptr) {
    return false;
  }

  FileStat file_stat_result;
  if (!file_handle_stat(&file->handle, &file_stat_result)) {
    return false;
  }

  return copy_file_stat_to_vfs_stat(&file_stat_result, out_stat);
}

size_t vfs_read_file(VfsFile* file, void* buffer, size_t bytes_to_read) {
  if (!vfs_file_is_open(file)) {
    return 0;
  }

  return file_read(&file->handle, buffer, bytes_to_read);
}

bool vfs_seek_file(VfsFile* file, uint32_t offset) {
  if (!vfs_file_is_open(file)) {
    return false;
  }

  return file_seek(&file->handle, offset);
}

uint32_t vfs_tell_file(const VfsFile* file) {
  if (!vfs_file_is_open(file)) {
    return 0;
  }

  return file_tell(&file->handle);
}

bool vfs_open_directory(const VfsMount* mount, const char* path,
                        VfsDirectory* out_directory) {
  if (out_directory == nullptr) {
    return false;
  }

  memory_set(out_directory, 0, sizeof(*out_directory));

  return vfs_is_mounted(mount) &&
         directory_open(mount->os64fs, path, &out_directory->handle);
}

bool vfs_directory_is_open(const VfsDirectory* directory) {
  return directory != nullptr &&
         directory_is_open(&directory->handle);
}

bool vfs_close_directory(VfsDirectory* directory) {
  if (!vfs_directory_is_open(directory)) {
    return false;
  }

  return directory_close(&directory->handle);
}

uint32_t vfs_directory_entry_count(const VfsDirectory* directory) {
  if (!vfs_directory_is_open(directory)) {
    return 0;
  }

  return directory_entry_count(&directory->handle);
}

bool vfs_read_directory(VfsDirectory* directory,
                        VfsDirectoryEntry* out_entry) {
  if (!vfs_directory_is_open(directory) || out_entry == nullptr) {
    return false;
  }

  DirectoryEntry directory_entry;
  if (!directory_read(&directory->handle, &directory_entry)) {
    return false;
  }

  return copy_directory_entry_to_vfs_entry(&directory_entry, out_entry);
}

bool vfs_rewind_directory(VfsDirectory* directory) {
  if (!vfs_directory_is_open(directory)) {
    return false;
  }

  return directory_rewind(&directory->handle);
}

uint32_t vfs_tell_directory(const VfsDirectory* directory) {
  if (!vfs_directory_is_open(directory)) {
    return 0;
  }

  return directory_tell(&directory->handle);
}
