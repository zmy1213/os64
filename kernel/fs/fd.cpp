#include "fs/fd.hpp"

#include "runtime/runtime.hpp"

namespace {

bool fd_index_is_valid(int32_t fd) {
  return fd >= 0 &&
         static_cast<size_t>(fd) < kFileDescriptorCapacity;
}

FileDescriptorEntry* mutable_entry(FileDescriptorTable* table, int32_t fd) {
  if (table == nullptr || !fd_index_is_valid(fd)) {
    return nullptr;
  }

  return &table->entries[static_cast<size_t>(fd)];
}

const FileDescriptorEntry* const_entry(const FileDescriptorTable* table,
                                       int32_t fd) {
  if (table == nullptr || !fd_index_is_valid(fd)) {
    return nullptr;
  }

  return &table->entries[static_cast<size_t>(fd)];
}

}  // namespace

bool initialize_file_descriptor_table(FileDescriptorTable* table,
                                      const VfsMount* vfs) {
  if (table == nullptr) {
    return false;
  }

  // fd 表必须从干净状态开始，否则一个旧槽位可能指向已经关闭的文件对象。
  memory_set(table, 0, sizeof(*table));

  if (!vfs_is_mounted(vfs)) {
    return false;
  }

  table->vfs = vfs;
  table->open_count = 0;
  return true;
}

bool file_descriptor_table_is_ready(const FileDescriptorTable* table) {
  return table != nullptr && vfs_is_mounted(table->vfs);
}

int32_t fd_open(FileDescriptorTable* table, const char* path) {
  if (!file_descriptor_table_is_ready(table) || path == nullptr) {
    return kInvalidFileDescriptor;
  }

  for (size_t i = 0; i < kFileDescriptorCapacity; ++i) {
    FileDescriptorEntry& entry = table->entries[i];
    if (entry.open) {
      continue;
    }

    // 先让 VFS 打开真实文件；只有打开成功后，这个槽位才正式占用。
    if (!vfs_open_file(table->vfs, path, &entry.file)) {
      return kInvalidFileDescriptor;
    }

    entry.open = true;
    ++table->open_count;
    return static_cast<int32_t>(i);
  }

  return kInvalidFileDescriptor;
}

bool fd_is_open(const FileDescriptorTable* table, int32_t fd) {
  const FileDescriptorEntry* const entry = const_entry(table, fd);
  return file_descriptor_table_is_ready(table) &&
         entry != nullptr &&
         entry->open &&
         vfs_file_is_open(&entry->file);
}

size_t fd_read(FileDescriptorTable* table, int32_t fd,
               void* buffer, size_t bytes_to_read) {
  if (!fd_is_open(table, fd)) {
    return 0;
  }

  FileDescriptorEntry* const entry = mutable_entry(table, fd);
  if (entry == nullptr) {
    return 0;
  }

  return vfs_read_file(&entry->file, buffer, bytes_to_read);
}

bool fd_close(FileDescriptorTable* table, int32_t fd) {
  if (!fd_is_open(table, fd)) {
    return false;
  }

  FileDescriptorEntry* const entry = mutable_entry(table, fd);
  if (entry == nullptr || !vfs_close_file(&entry->file)) {
    return false;
  }

  memory_set(entry, 0, sizeof(*entry));
  if (table->open_count > 0) {
    --table->open_count;
  }

  return true;
}

bool fd_stat(const FileDescriptorTable* table, int32_t fd,
             VfsStat* out_stat) {
  if (!fd_is_open(table, fd) || out_stat == nullptr) {
    return false;
  }

  const FileDescriptorEntry* const entry = const_entry(table, fd);
  if (entry == nullptr) {
    return false;
  }

  return vfs_file_stat(&entry->file, out_stat);
}

bool fd_seek(FileDescriptorTable* table, int32_t fd, uint32_t offset) {
  if (!fd_is_open(table, fd)) {
    return false;
  }

  FileDescriptorEntry* const entry = mutable_entry(table, fd);
  if (entry == nullptr) {
    return false;
  }

  return vfs_seek_file(&entry->file, offset);
}

uint32_t fd_tell(const FileDescriptorTable* table, int32_t fd) {
  if (!fd_is_open(table, fd)) {
    return 0;
  }

  const FileDescriptorEntry* const entry = const_entry(table, fd);
  if (entry == nullptr) {
    return 0;
  }

  return vfs_tell_file(&entry->file);
}

uint32_t fd_open_count(const FileDescriptorTable* table) {
  if (!file_descriptor_table_is_ready(table)) {
    return 0;
  }

  return table->open_count;
}
