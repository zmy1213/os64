#include "fs/os64fs.hpp"

#include "runtime/runtime.hpp"

namespace {

constexpr char kOs64FsSignature[8] = {
    'O', 'S', '6', '4', 'F', 'S', 'V', '1',
};
constexpr size_t kMaxPathDepth = 16;  // 第一版路径栈先限制 16 层，避免在内核里引入动态分配。

bool signature_matches(const char* actual) {
  if (actual == nullptr) {
    return false;
  }

  for (size_t i = 0; i < sizeof(kOs64FsSignature); ++i) {
    if (actual[i] != kOs64FsSignature[i]) {
      return false;
    }
  }

  return true;
}

bool is_path_separator(char ch) {
  return ch == '/';
}

const char* skip_path_separators(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }

  while (is_path_separator(*path)) {
    ++path;
  }

  return path;
}

size_t component_length(const char* begin) {
  size_t length = 0;
  while (begin[length] != '\0' && !is_path_separator(begin[length])) {
    ++length;
  }

  return length;
}

bool name_matches(const Os64FsDirEntry* entry,
                  const char* name,
                  size_t name_length) {
  if (entry == nullptr || name == nullptr ||
      entry->name_length != name_length ||
      name_length > sizeof(entry->name)) {
    return false;
  }

  for (size_t i = 0; i < name_length; ++i) {
    if (entry->name[i] != name[i]) {
      return false;
    }
  }

  return true;
}

bool component_is_dot(const char* component, size_t component_name_length) {
  return component != nullptr &&
         component_name_length == 1 &&
         component[0] == '.';
}

bool component_is_dot_dot(const char* component,
                          size_t component_name_length) {
  return component != nullptr &&
         component_name_length == 2 &&
         component[0] == '.' &&
         component[1] == '.';
}

bool read_superblock(Os64Fs* filesystem) {
  uint8_t sector[512];
  if (!block_device_read_sector(filesystem->device, 0, sector, sizeof(sector))) {
    return false;
  }

  memory_copy(&filesystem->superblock, sector, sizeof(filesystem->superblock));
  return true;
}

bool read_inode_table(Os64Fs* filesystem) {
  return block_device_read_sector(filesystem->device,
                                  filesystem->superblock.inode_table_start_sector,
                                  filesystem->inode_table_cache,
                                  sizeof(filesystem->inode_table_cache));
}

bool filesystem_layout_is_valid(const Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->device == nullptr) {
    return false;
  }

  const Os64FsSuperblock& sb = filesystem->superblock;
  if (!signature_matches(sb.signature) ||
      sb.version != kOs64FsVersion ||
      sb.total_sectors != filesystem->device->sector_count ||
      sb.inode_size != sizeof(Os64FsInode) ||
      sb.inode_count == 0 ||
      sb.root_inode == 0 ||
      sb.root_inode >= sb.inode_count ||
      sb.inode_table_start_sector >= sb.total_sectors ||
      sb.data_start_sector >= sb.total_sectors ||
      sb.data_sector_count == 0 ||
      sb.data_start_sector + sb.data_sector_count > sb.total_sectors ||
      sb.data_block_size == 0 ||
      sb.data_block_size > filesystem->device->sector_size ||
      (filesystem->device->sector_size % sb.data_block_size) != 0) {
    return false;
  }

  const uint32_t inode_table_bytes = sb.inode_count * sb.inode_size;
  if (inode_table_bytes > filesystem->device->sector_size) {
    return false;
  }

  return true;
}

bool inode_is_valid(const Os64Fs* filesystem, const Os64FsInode* inode) {
  if (!os64fs_is_mounted(filesystem) || inode == nullptr ||
      inode->inode_number == 0 ||
      inode->inode_number >= filesystem->superblock.inode_count) {
    return false;
  }

  return inode->type == kOs64FsTypeFile ||
         inode->type == kOs64FsTypeDirectory;
}

bool read_inode_block_bytes(const Os64Fs* filesystem,
                            const Os64FsInode* inode,
                            uint32_t offset,
                            void* buffer,
                            size_t bytes_to_read) {
  if (!inode_is_valid(filesystem, inode) || buffer == nullptr) {
    return false;
  }

  if (bytes_to_read == 0) {
    return true;
  }

  if (offset > inode->size_bytes ||
      bytes_to_read > (inode->size_bytes - offset)) {
    return false;
  }

  const uint32_t block_size = filesystem->superblock.data_block_size;
  const uint32_t sector_size = filesystem->device->sector_size;
  uint8_t sector_buffer[512];
  uint32_t cached_sector = 0xFFFFFFFFu;
  auto* destination = static_cast<uint8_t*>(buffer);
  size_t copied = 0;

  while (copied < bytes_to_read) {
    const uint32_t absolute_offset = offset + static_cast<uint32_t>(copied);
    const uint32_t file_block_slot = absolute_offset / block_size;
    const uint32_t block_offset = absolute_offset % block_size;

    if (file_block_slot >= 4) {
      return false;
    }

    const uint32_t block_index = inode->direct_blocks[file_block_slot];
    if (block_index == kOs64FsInvalidBlockIndex) {
      return false;
    }

    const uint32_t block_byte_offset = block_index * block_size + block_offset;
    const uint32_t sector_index =
        filesystem->superblock.data_start_sector +
        (block_byte_offset / sector_size);
    const uint32_t sector_offset = block_byte_offset % sector_size;

    if (sector_index >= filesystem->superblock.total_sectors) {
      return false;
    }

    if (cached_sector != sector_index) {
      if (!block_device_read_sector(filesystem->device, sector_index,
                                    sector_buffer, sizeof(sector_buffer))) {
        return false;
      }
      cached_sector = sector_index;
    }

    uint32_t bytes_this_round = block_size - block_offset;
    if (bytes_this_round > (sector_size - sector_offset)) {
      bytes_this_round = sector_size - sector_offset;
    }

    const size_t remaining = bytes_to_read - copied;
    if (bytes_this_round > remaining) {
      bytes_this_round = static_cast<uint32_t>(remaining);
    }

    memory_copy(destination + copied, sector_buffer + sector_offset,
                bytes_this_round);
    copied += bytes_this_round;
  }

  return true;
}

bool find_child_inode(const Os64Fs* filesystem,
                      const Os64FsInode* directory_inode,
                      const char* component,
                      size_t component_name_length,
                      uint32_t* out_inode_number) {
  if (!inode_is_valid(filesystem, directory_inode) ||
      directory_inode->type != kOs64FsTypeDirectory ||
      component == nullptr || component_name_length == 0 ||
      out_inode_number == nullptr) {
    return false;
  }

  const uint32_t entry_count =
      os64fs_directory_entry_count(filesystem, directory_inode);
  for (uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
    Os64FsDirEntry entry;
    if (!os64fs_read_directory_entry(filesystem, directory_inode, entry_index,
                                     &entry)) {
      return false;
    }

    if (name_matches(&entry, component, component_name_length)) {
      *out_inode_number = entry.inode_number;
      return true;
    }
  }

  return false;
}

}  // namespace

bool initialize_os64fs(Os64Fs* filesystem, const BlockDevice* device) {
  if (filesystem == nullptr || !block_device_is_ready(device)) {
    return false;
  }

  memory_set(filesystem, 0, sizeof(*filesystem));
  filesystem->device = device;

  if (!read_superblock(filesystem) ||
      !filesystem_layout_is_valid(filesystem) ||
      !read_inode_table(filesystem)) {
    filesystem->device = nullptr;
    return false;
  }

  filesystem->mounted = true;

  Os64FsInode root_inode;
  if (!os64fs_read_inode(filesystem, filesystem->superblock.root_inode,
                         &root_inode) ||
      root_inode.type != kOs64FsTypeDirectory) {
    filesystem->mounted = false;
    filesystem->device = nullptr;
    return false;
  }

  return true;
}

bool os64fs_is_mounted(const Os64Fs* filesystem) {
  return filesystem != nullptr &&
         filesystem->device != nullptr &&
         filesystem->mounted;
}

const Os64FsSuperblock* os64fs_superblock(const Os64Fs* filesystem) {
  if (!os64fs_is_mounted(filesystem)) {
    return nullptr;
  }

  return &filesystem->superblock;
}

const char* os64fs_inode_type_name(uint16_t type) {
  switch (type) {
    case kOs64FsTypeFile:
      return "file";
    case kOs64FsTypeDirectory:
      return "dir";
    default:
      return "unknown";
  }
}

bool os64fs_read_inode(const Os64Fs* filesystem, uint32_t inode_number,
                       Os64FsInode* out_inode) {
  if (!os64fs_is_mounted(filesystem) || out_inode == nullptr ||
      inode_number == 0 ||
      inode_number >= filesystem->superblock.inode_count) {
    return false;
  }

  const uint32_t inode_offset =
      inode_number * filesystem->superblock.inode_size;
  const uint32_t inode_end = inode_offset + filesystem->superblock.inode_size;
  if (inode_end > sizeof(filesystem->inode_table_cache)) {
    return false;
  }

  memory_copy(out_inode, filesystem->inode_table_cache + inode_offset,
              sizeof(Os64FsInode));
  return inode_is_valid(filesystem, out_inode) &&
         out_inode->inode_number == inode_number;
}

bool os64fs_lookup_path(const Os64Fs* filesystem, const char* path,
                        Os64FsInode* out_inode) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr ||
      out_inode == nullptr) {
    return false;
  }

  uint32_t inode_stack[kMaxPathDepth];
  size_t depth = 1;
  inode_stack[0] = filesystem->superblock.root_inode;

  Os64FsInode current_inode;
  if (!os64fs_read_inode(filesystem, inode_stack[0], &current_inode)) {
    return false;
  }

  const char* cursor = skip_path_separators(path);
  if (cursor == nullptr || cursor[0] == '\0') {
    *out_inode = current_inode;
    return true;
  }

  while (cursor[0] != '\0') {
    const size_t current_component_length = component_length(cursor);
    if (current_component_length == 0) {
      break;
    }

    if (current_inode.type != kOs64FsTypeDirectory) {
      return false;
    }

    if (component_is_dot(cursor, current_component_length)) {
      cursor = skip_path_separators(cursor + current_component_length);
      continue;
    }

    if (component_is_dot_dot(cursor, current_component_length)) {
      if (depth > 1) {
        --depth;
      }

      if (!os64fs_read_inode(filesystem, inode_stack[depth - 1],
                             &current_inode)) {
        return false;
      }

      cursor = skip_path_separators(cursor + current_component_length);
      continue;
    }

    uint32_t next_inode_number = 0;
    if (!find_child_inode(filesystem, &current_inode, cursor,
                          current_component_length, &next_inode_number) ||
        !os64fs_read_inode(filesystem, next_inode_number, &current_inode)) {
      return false;
    }

    if (depth >= kMaxPathDepth) {
      return false;
    }

    inode_stack[depth++] = next_inode_number;

    cursor = skip_path_separators(cursor + current_component_length);
  }

  *out_inode = current_inode;
  return true;
}

uint32_t os64fs_directory_entry_count(const Os64Fs* filesystem,
                                      const Os64FsInode* directory_inode) {
  if (!inode_is_valid(filesystem, directory_inode) ||
      directory_inode->type != kOs64FsTypeDirectory ||
      (directory_inode->size_bytes % sizeof(Os64FsDirEntry)) != 0) {
    return 0;
  }

  return directory_inode->size_bytes / sizeof(Os64FsDirEntry);
}

bool os64fs_read_directory_entry(const Os64Fs* filesystem,
                                 const Os64FsInode* directory_inode,
                                 uint32_t entry_index,
                                 Os64FsDirEntry* out_entry) {
  if (out_entry == nullptr) {
    return false;
  }

  const uint32_t entry_count =
      os64fs_directory_entry_count(filesystem, directory_inode);
  if (entry_index >= entry_count) {
    return false;
  }

  const uint32_t entry_offset = entry_index * sizeof(Os64FsDirEntry);
  if (!read_inode_block_bytes(filesystem, directory_inode, entry_offset,
                              out_entry, sizeof(Os64FsDirEntry))) {
    return false;
  }

  return out_entry->inode_number != 0 &&
         out_entry->name_length <= sizeof(out_entry->name);
}

bool os64fs_read_inode_data(const Os64Fs* filesystem,
                            const Os64FsInode* inode,
                            uint32_t offset,
                            void* buffer,
                            size_t bytes_to_read) {
  return read_inode_block_bytes(filesystem, inode, offset,
                                buffer, bytes_to_read);
}
