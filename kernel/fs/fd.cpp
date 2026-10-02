#include "fs/fd.hpp"

#include "runtime/runtime.hpp"
#include "memory/kmemory.hpp"

namespace {

// 先确认一个 fd 小整数有没有落在当前表容量范围内。
bool fd_index_is_valid(int32_t fd) {
  return fd >= 0 &&
         static_cast<size_t>(fd) < kFileDescriptorCapacity;
}

// 取可写槽位指针。
FileDescriptorEntry* mutable_entry(FileDescriptorTable* table, int32_t fd) {
  if (table == nullptr || !fd_index_is_valid(fd)) {
    return nullptr;
  }

  return &table->entries[static_cast<size_t>(fd)];
}

// 取只读槽位指针。
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

int32_t fd_open(FileDescriptorTable* table, const char* path, uint32_t flags) {
  if (!file_descriptor_table_is_ready(table) || path == nullptr) {
    return kInvalidFileDescriptor;
  }

  // 从前往后找第一个空槽位。
  for (size_t i = 0; i < kFileDescriptorCapacity; ++i) {
    FileDescriptorEntry& entry = table->entries[i];
    if (entry.open) {
      continue;
    }

    size_t path_length = 0;
    while (path_length < kFileDescriptorPathCapacity && path[path_length] != '\0') {
      ++path_length;
    }
    if (path_length == 0 || path_length >= kFileDescriptorPathCapacity) {
      return kInvalidFileDescriptor;
    }
    // A free descriptor must exist before create/truncate can change a file.
    VfsMount mutable_mount = *table->vfs;
    VfsStat stat;
    if (!vfs_stat(table->vfs, path, &stat)) {
      if ((flags & kOpenCreate) == 0 || !vfs_create_file(&mutable_mount, path)) {
        return kInvalidFileDescriptor;
      }
    } else if (stat.type != kVfsNodeTypeFile) {
      return kInvalidFileDescriptor;
    }
    if ((flags & kOpenTruncate) != 0 &&
        !vfs_write_file(&mutable_mount, path, nullptr, 0)) {
      return kInvalidFileDescriptor;
    }

    // 先让 VFS 打开真实文件；只有打开成功后，这个槽位才正式占用。
    if (!vfs_open_file(table->vfs, path, &entry.file)) {
      return kInvalidFileDescriptor;
    }

    entry.open = true;
    entry.flags = flags;
    memory_copy(entry.path, path, path_length + 1);
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
  if (!fd_can_read(table, fd)) {
    return 0;
  }

  FileDescriptorEntry* const entry = mutable_entry(table, fd);
  if (entry == nullptr) {
    return 0;
  }

  return vfs_read_file(&entry->file, buffer, bytes_to_read);
}

bool fd_can_read(const FileDescriptorTable* table, int32_t fd) {
  const FileDescriptorEntry* entry = const_entry(table, fd);
  return fd_is_open(table, fd) && (entry->flags & kOpenRead) != 0;
}

bool fd_can_write(const FileDescriptorTable* table, int32_t fd) {
  const FileDescriptorEntry* entry = const_entry(table, fd);
  return fd_is_open(table, fd) && (entry->flags & kOpenWrite) != 0;
}

int32_t fd_write(FileDescriptorTable* table, int32_t fd,
                 const void* buffer, size_t bytes_to_write) {
  if (!fd_can_write(table, fd) || (buffer == nullptr && bytes_to_write != 0)) {
    return -1;
  }
  if (bytes_to_write == 0) {
    return 0;
  }
  FileDescriptorEntry* entry = mutable_entry(table, fd);
  Os64FsInode inode;
  Os64Fs* fs = table->vfs->os64fs;
  if (!os64fs_lookup_path(fs, entry->path, &inode) ||
      inode.inode_number != entry->file.handle.inode.inode_number) {
    return -1;
  }
  const uint32_t offset = (entry->flags & kOpenAppend) != 0
                              ? inode.size_bytes : entry->file.handle.offset;
  const uint64_t maximum = (kOs64FsDirectBlockCount + 128ULL) * 512ULL;
  if (bytes_to_write > maximum || offset > maximum - bytes_to_write) {
    return -1;
  }
  const uint64_t end = static_cast<uint64_t>(offset) + bytes_to_write;
  if (end > maximum || end > 2147483647ULL) {
    return -1;
  }
  const size_t size = end > inode.size_bytes ? static_cast<size_t>(end)
                                            : inode.size_bytes;
  auto* staging = static_cast<uint8_t*>(kcalloc(size, 1));
  if (staging == nullptr) {
    return -1;
  }
  bool ok = inode.size_bytes == 0 ||
            os64fs_read_inode_data(fs, &inode, 0, staging, inode.size_bytes);
  if (ok) {
    memory_copy(staging + offset, buffer, bytes_to_write);
    ok = os64fs_write_file(fs, entry->path, staging, size);
  }
  kfree(staging);
  if (!ok || !os64fs_lookup_path(fs, entry->path, &inode)) {
    return -1;
  }
  entry->file.handle.inode = inode;
  entry->file.handle.offset = static_cast<uint32_t>(end);
  return static_cast<int32_t>(bytes_to_write);
}

bool fd_close(FileDescriptorTable* table, int32_t fd) {
  const auto* existing = const_entry(table, fd);
  if (existing == nullptr || !existing->open) {
    return false;
  }

  FileDescriptorEntry* const entry = mutable_entry(table, fd);
  if (entry == nullptr || !vfs_close_file(&entry->file)) {
    return false;
  }

  // 真正关闭成功后，把槽位清空，表示这个 fd 编号可以再次复用。
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
