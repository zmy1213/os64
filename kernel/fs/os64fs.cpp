#include "fs/os64fs.hpp"

#include "runtime/runtime.hpp"

namespace {

constexpr char kOs64FsSignature[8] = {
    'O', 'S', '6', '4', 'F', 'S', 'V', '3',
};

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

uint32_t filesystem_data_block_count(const Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->device == nullptr ||
      filesystem->superblock.data_block_size == 0) {
    return 0;
  }

  const uint64_t total_data_bytes =
      static_cast<uint64_t>(filesystem->superblock.data_sector_count) *
      filesystem->device->sector_size;
  return static_cast<uint32_t>(
      total_data_bytes / filesystem->superblock.data_block_size);
}

uint32_t filesystem_indirect_entry_capacity(const Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->superblock.data_block_size == 0) {
    return 0;
  }

  return filesystem->superblock.data_block_size / sizeof(uint32_t);
}

uint32_t bitmap_required_bytes(uint32_t bit_count) {
  return (bit_count + 7u) / 8u;
}

bool bitmap_bit_is_set(const uint8_t* bitmap,
                       uint32_t bitmap_bytes,
                       uint32_t bit_index) {
  if (bitmap == nullptr) {
    return false;
  }

  const uint32_t byte_index = bit_index / 8u;
  const uint8_t bit_mask = static_cast<uint8_t>(1u << (bit_index % 8u));
  return byte_index < bitmap_bytes &&
         (bitmap[byte_index] & bit_mask) != 0;
}

void bitmap_set_bit(uint8_t* bitmap,
                    uint32_t bitmap_bytes,
                    uint32_t bit_index) {
  if (bitmap == nullptr) {
    return;
  }

  const uint32_t byte_index = bit_index / 8u;
  if (byte_index >= bitmap_bytes) {
    return;
  }

  bitmap[byte_index] |= static_cast<uint8_t>(1u << (bit_index % 8u));
}

void bitmap_clear_bit(uint8_t* bitmap,
                      uint32_t bitmap_bytes,
                      uint32_t bit_index) {
  if (bitmap == nullptr) {
    return;
  }

  const uint32_t byte_index = bit_index / 8u;
  if (byte_index >= bitmap_bytes) {
    return;
  }

  bitmap[byte_index] &= static_cast<uint8_t>(~(1u << (bit_index % 8u)));
}

bool data_block_index_is_valid(const Os64Fs* filesystem, uint32_t block_index) {
  return filesystem != nullptr &&
         block_index != kOs64FsInvalidBlockIndex &&
         block_index < filesystem_data_block_count(filesystem);
}

bool read_superblock(Os64Fs* filesystem) {
  uint8_t sector[512];
  if (!block_device_read_sector(filesystem->device, 0, sector, sizeof(sector))) {
    return false;
  }

  memory_copy(&filesystem->superblock, sector, sizeof(filesystem->superblock));
  return true;
}

bool write_superblock(Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->device == nullptr) {
    return false;
  }

  uint8_t sector[512];
  memory_set(sector, 0, sizeof(sector));
  memory_copy(sector, &filesystem->superblock, sizeof(filesystem->superblock));
  return block_device_write_sector(filesystem->device, 0,
                                   sector, sizeof(sector));
}

bool read_inode_table(Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->device == nullptr) {
    return false;
  }

  const uint32_t sector_size = filesystem->device->sector_size;
  const uint32_t inode_table_sector_count =
      filesystem->superblock.inode_table_sector_count;
  const uint32_t inode_table_bytes =
      filesystem->superblock.inode_count * filesystem->superblock.inode_size;
  const uint32_t cached_bytes = inode_table_sector_count * sector_size;
  if (inode_table_bytes == 0 ||
      inode_table_bytes > kOs64FsMaxInodeTableBytes ||
      cached_bytes > kOs64FsMaxInodeTableBytes) {
    return false;
  }

  memory_set(filesystem->inode_table_cache, 0,
             sizeof(filesystem->inode_table_cache));
  for (uint32_t sector_offset = 0;
       sector_offset < inode_table_sector_count;
       ++sector_offset) {
    if (!block_device_read_sector(filesystem->device,
                                  filesystem->superblock.inode_table_start_sector +
                                      sector_offset,
                                  filesystem->inode_table_cache +
                                      sector_offset * sector_size,
                                  sector_size)) {
      return false;
    }
  }

  filesystem->inode_table_bytes = inode_table_bytes;
  return true;
}

bool read_bitmap(Os64Fs* filesystem,
                 uint32_t start_sector,
                 uint32_t sector_count,
                 uint32_t required_bytes,
                 uint8_t* cache,
                 uint32_t cache_capacity,
                 uint32_t* out_cached_bytes) {
  if (filesystem == nullptr || filesystem->device == nullptr ||
      cache == nullptr || out_cached_bytes == nullptr) {
    return false;
  }

  const uint32_t sector_size = filesystem->device->sector_size;
  const uint32_t cached_bytes = sector_count * sector_size;
  if (required_bytes == 0 ||
      required_bytes > cache_capacity ||
      cached_bytes > cache_capacity ||
      required_bytes > cached_bytes) {
    return false;
  }

  memory_set(cache, 0, cache_capacity);
  for (uint32_t sector_offset = 0; sector_offset < sector_count;
       ++sector_offset) {
    if (!block_device_read_sector(filesystem->device,
                                  start_sector + sector_offset,
                                  cache + sector_offset * sector_size,
                                  sector_size)) {
      return false;
    }
  }

  *out_cached_bytes = required_bytes;
  return true;
}

bool write_cached_sectors(Os64Fs* filesystem,
                          uint32_t start_sector,
                          uint32_t sector_count,
                          const uint8_t* cache) {
  if (filesystem == nullptr || filesystem->device == nullptr ||
      cache == nullptr) {
    return false;
  }

  const uint32_t sector_size = filesystem->device->sector_size;
  for (uint32_t sector_offset = 0; sector_offset < sector_count;
       ++sector_offset) {
    if (!block_device_write_sector(filesystem->device,
                                   start_sector + sector_offset,
                                   cache + sector_offset * sector_size,
                                   sector_size)) {
      return false;
    }
  }

  return true;
}

bool write_inode_table(Os64Fs* filesystem) {
  return filesystem != nullptr &&
         write_cached_sectors(filesystem,
                              filesystem->superblock.inode_table_start_sector,
                              filesystem->superblock.inode_table_sector_count,
                              filesystem->inode_table_cache);
}

bool write_inode_bitmap(Os64Fs* filesystem) {
  return filesystem != nullptr &&
         write_cached_sectors(filesystem,
                              filesystem->superblock.inode_bitmap_start_sector,
                              filesystem->superblock.inode_bitmap_sector_count,
                              filesystem->inode_bitmap_cache);
}

bool write_data_bitmap(Os64Fs* filesystem) {
  return filesystem != nullptr &&
         write_cached_sectors(filesystem,
                              filesystem->superblock.data_bitmap_start_sector,
                              filesystem->superblock.data_bitmap_sector_count,
                              filesystem->data_bitmap_cache);
}

bool filesystem_layout_is_valid(const Os64Fs* filesystem) {
  if (filesystem == nullptr || filesystem->device == nullptr) {
    return false;
  }

  const Os64FsSuperblock& sb = filesystem->superblock;
  const uint32_t sector_size = filesystem->device->sector_size;
  const uint32_t total_data_blocks =
      filesystem_data_block_count(filesystem);
  const uint32_t inode_bitmap_required_bytes =
      bitmap_required_bytes(sb.inode_count);
  const uint32_t data_bitmap_required_bytes =
      bitmap_required_bytes(total_data_blocks);
  if (!signature_matches(sb.signature) ||
      sb.version != kOs64FsVersion ||
      sb.total_sectors != filesystem->device->sector_count ||
      sb.inode_bitmap_start_sector == 0 ||
      sb.inode_bitmap_sector_count == 0 ||
      sb.data_bitmap_start_sector == 0 ||
      sb.data_bitmap_sector_count == 0 ||
      sb.inode_size != sizeof(Os64FsInode) ||
      sb.directory_entry_size != sizeof(Os64FsDirEntry) ||
      sb.inode_count <= sb.root_inode ||
      sb.root_inode == 0 ||
      sb.inode_table_start_sector == 0 ||
      sb.inode_table_sector_count == 0 ||
      sb.data_start_sector == 0 ||
      sb.inode_bitmap_start_sector >= sb.total_sectors ||
      sb.data_bitmap_start_sector >= sb.total_sectors ||
      sb.inode_table_start_sector >= sb.total_sectors ||
      sb.data_start_sector >= sb.total_sectors ||
      sb.data_sector_count == 0 ||
      sb.data_start_sector + sb.data_sector_count > sb.total_sectors ||
      sb.data_block_size == 0 ||
      sb.data_block_size < sector_size ||
      (sb.data_block_size % sector_size) != 0 ||
      sb.free_inode_count > (sb.inode_count - 1)) {
    return false;
  }

  const uint32_t inode_table_bytes = sb.inode_count * sb.inode_size;
  const uint32_t inode_table_capacity_bytes =
      sb.inode_table_sector_count * sector_size;
  const uint32_t inode_bitmap_capacity_bytes =
      sb.inode_bitmap_sector_count * sector_size;
  const uint32_t data_bitmap_capacity_bytes =
      sb.data_bitmap_sector_count * sector_size;
  if (inode_table_bytes == 0 ||
      inode_table_bytes > inode_table_capacity_bytes ||
      inode_table_capacity_bytes > kOs64FsMaxInodeTableBytes ||
      inode_bitmap_required_bytes == 0 ||
      inode_bitmap_required_bytes > inode_bitmap_capacity_bytes ||
      inode_bitmap_capacity_bytes > kOs64FsMaxBitmapBytes ||
      data_bitmap_required_bytes == 0 ||
      data_bitmap_required_bytes > data_bitmap_capacity_bytes ||
      data_bitmap_capacity_bytes > kOs64FsMaxBitmapBytes) {
    return false;
  }

  const uint32_t inode_bitmap_end_sector =
      sb.inode_bitmap_start_sector + sb.inode_bitmap_sector_count;
  const uint32_t data_bitmap_end_sector =
      sb.data_bitmap_start_sector + sb.data_bitmap_sector_count;
  const uint32_t inode_table_end_sector =
      sb.inode_table_start_sector + sb.inode_table_sector_count;
  if (inode_bitmap_end_sector > sb.data_bitmap_start_sector ||
      data_bitmap_end_sector > sb.inode_table_start_sector ||
      inode_table_end_sector > sb.data_start_sector) {
    return false;
  }

  const uint64_t data_bytes =
      static_cast<uint64_t>(sb.data_sector_count) * sector_size;
  if (data_bytes < sb.data_block_size ||
      (data_bytes % sb.data_block_size) != 0 ||
      sb.free_data_block_count > total_data_blocks) {
    return false;
  }

  return true;
}

bool inode_is_valid(const Os64Fs* filesystem, const Os64FsInode* inode) {
  if (!os64fs_is_mounted(filesystem) || inode == nullptr ||
      inode->inode_number == 0 ||
      inode->inode_number >= filesystem->superblock.inode_count ||
      (inode->type != kOs64FsTypeFile &&
       inode->type != kOs64FsTypeDirectory)) {
    return false;
  }

  const uint32_t required_blocks =
      inode->size_bytes == 0
          ? 0
          : ((inode->size_bytes - 1) / filesystem->superblock.data_block_size) + 1;
  const uint32_t max_indirect_entries =
      filesystem_indirect_entry_capacity(filesystem);
  const uint32_t max_block_count =
      static_cast<uint32_t>(kOs64FsDirectBlockCount) + max_indirect_entries;
  if (inode->block_count < required_blocks ||
      inode->block_count > max_block_count) {
    return false;
  }

  for (size_t i = 0; i < kOs64FsDirectBlockCount; ++i) {
    if (i < inode->block_count) {
      if (!data_block_index_is_valid(filesystem, inode->direct_blocks[i])) {
        return false;
      }
      continue;
    }

    if (inode->direct_blocks[i] != kOs64FsInvalidBlockIndex) {
      return false;
    }
  }

  if (inode->block_count > kOs64FsDirectBlockCount) {
    return data_block_index_is_valid(filesystem, inode->indirect_block);
  }

  return inode->indirect_block == kOs64FsInvalidBlockIndex;
}

bool read_u32_from_data_block(const Os64Fs* filesystem,
                              uint32_t block_index,
                              uint32_t byte_offset_in_block,
                              uint32_t* out_value) {
  if (!data_block_index_is_valid(filesystem, block_index) ||
      out_value == nullptr ||
      byte_offset_in_block + sizeof(uint32_t) >
          filesystem->superblock.data_block_size) {
    return false;
  }

  const uint32_t sector_size = filesystem->device->sector_size;
  const uint32_t byte_offset_in_volume =
      block_index * filesystem->superblock.data_block_size +
      byte_offset_in_block;
  const uint32_t sector_index =
      filesystem->superblock.data_start_sector +
      (byte_offset_in_volume / sector_size);
  const uint32_t sector_offset = byte_offset_in_volume % sector_size;
  if (sector_index >= filesystem->superblock.total_sectors ||
      sector_offset + sizeof(uint32_t) > sector_size) {
    return false;
  }

  uint8_t sector_buffer[512];
  if (!block_device_read_sector(filesystem->device, sector_index,
                                sector_buffer, sizeof(sector_buffer))) {
    return false;
  }

  memory_copy(out_value, sector_buffer + sector_offset, sizeof(uint32_t));
  return true;
}

bool inode_block_index_for_file_block(const Os64Fs* filesystem,
                                      const Os64FsInode* inode,
                                      uint32_t file_block_index,
                                      uint32_t* out_block_index) {
  if (!inode_is_valid(filesystem, inode) || out_block_index == nullptr ||
      file_block_index >= inode->block_count) {
    return false;
  }

  if (file_block_index < kOs64FsDirectBlockCount) {
    *out_block_index = inode->direct_blocks[file_block_index];
    return data_block_index_is_valid(filesystem, *out_block_index);
  }

  const uint32_t indirect_block_slot =
      file_block_index - static_cast<uint32_t>(kOs64FsDirectBlockCount);
  if (indirect_block_slot >= filesystem_indirect_entry_capacity(filesystem)) {
    return false;
  }

  if (!read_u32_from_data_block(filesystem, inode->indirect_block,
                                indirect_block_slot * sizeof(uint32_t),
                                out_block_index)) {
    return false;
  }

  return data_block_index_is_valid(filesystem, *out_block_index);
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

    uint32_t block_index = 0;
    if (!inode_block_index_for_file_block(filesystem, inode, file_block_slot,
                                          &block_index)) {
      return false;
    }

    const uint32_t block_byte_offset =
        block_index * block_size + block_offset;
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

const Os64FsInode* cached_inode_pointer(const Os64Fs* filesystem,
                                        uint32_t inode_number) {
  if (filesystem == nullptr || inode_number >= filesystem->superblock.inode_count) {
    return nullptr;
  }

  const uint32_t inode_offset =
      inode_number * filesystem->superblock.inode_size;
  const uint32_t inode_end = inode_offset + filesystem->superblock.inode_size;
  if (inode_end > filesystem->inode_table_bytes) {
    return nullptr;
  }

  return reinterpret_cast<const Os64FsInode*>(
      filesystem->inode_table_cache + inode_offset);
}

bool inode_bytes_are_zero(const Os64FsInode* inode) {
  if (inode == nullptr) {
    return false;
  }

  const auto* bytes = reinterpret_cast<const uint8_t*>(inode);
  for (size_t i = 0; i < sizeof(Os64FsInode); ++i) {
    if (bytes[i] != 0) {
      return false;
    }
  }

  return true;
}

void set_validation_debug(Os64Fs* filesystem,
                          uint32_t code,
                          uint32_t index,
                          uint32_t expected,
                          uint32_t actual) {
  if (filesystem == nullptr) {
    return;
  }

  filesystem->validation_debug.code = code;
  filesystem->validation_debug.index = index;
  filesystem->validation_debug.expected = expected;
  filesystem->validation_debug.actual = actual;
}

bool mark_data_block_allocated(Os64Fs* filesystem,
                               uint8_t* seen_blocks,
                               uint32_t seen_block_bytes,
                               uint32_t block_index,
                               uint32_t* used_block_count) {
  if (!data_block_index_is_valid(filesystem, block_index) ||
      seen_blocks == nullptr || used_block_count == nullptr) {
    set_validation_debug(filesystem, kOs64FsValidationDebugDataBlockReference,
                         block_index, 1, 0);
    return false;
  }

  if (bitmap_bit_is_set(seen_blocks, seen_block_bytes, block_index)) {
    set_validation_debug(filesystem, kOs64FsValidationDebugDataBlockReference,
                         block_index, 0, 2);
    return false;
  }

  bitmap_set_bit(seen_blocks, seen_block_bytes, block_index);
  ++(*used_block_count);
  return true;
}

bool validate_allocation_maps_and_collect_stats(Os64Fs* filesystem) {
  if (filesystem == nullptr || !os64fs_is_mounted(filesystem)) {
    return false;
  }

  set_validation_debug(filesystem, kOs64FsValidationDebugOk, 0, 0, 0);

  const uint32_t total_data_blocks = filesystem_data_block_count(filesystem);
  if (!bitmap_bit_is_set(filesystem->inode_bitmap_cache,
                         filesystem->inode_bitmap_bytes, 0)) {
    set_validation_debug(filesystem, kOs64FsValidationDebugInode0Bitmap,
                         0, 1, 0);
    return false;
  }

  uint8_t seen_data_blocks[kOs64FsMaxBitmapBytes];
  memory_set(seen_data_blocks, 0, sizeof(seen_data_blocks));

  uint32_t used_inode_count = 0;
  uint32_t used_data_block_count = 0;
  for (uint32_t inode_number = 1;
       inode_number < filesystem->superblock.inode_count;
       ++inode_number) {
    const Os64FsInode* const cached_inode =
        cached_inode_pointer(filesystem, inode_number);
    if (cached_inode == nullptr) {
      set_validation_debug(filesystem, kOs64FsValidationDebugInodeCacheRange,
                           inode_number, 1, 0);
      return false;
    }

    const bool inode_allocated =
        bitmap_bit_is_set(filesystem->inode_bitmap_cache,
                          filesystem->inode_bitmap_bytes, inode_number);
    if (!inode_allocated) {
      if (!inode_bytes_are_zero(cached_inode)) {
        set_validation_debug(filesystem, kOs64FsValidationDebugFreedInodeNotZero,
                             inode_number, 0, 1);
        return false;
      }
      continue;
    }

    Os64FsInode inode;
    memory_copy(&inode, cached_inode, sizeof(inode));
    if (!inode_is_valid(filesystem, &inode) ||
        inode.inode_number != inode_number) {
      set_validation_debug(filesystem, kOs64FsValidationDebugAllocatedInodeBad,
                           inode_number, inode_number, inode.inode_number);
      return false;
    }

    ++used_inode_count;

    const uint32_t direct_block_count =
        inode.block_count < kOs64FsDirectBlockCount
            ? inode.block_count
            : static_cast<uint32_t>(kOs64FsDirectBlockCount);
    for (uint32_t direct_index = 0;
         direct_index < direct_block_count;
         ++direct_index) {
      if (!mark_data_block_allocated(filesystem, seen_data_blocks,
                                     filesystem->data_bitmap_bytes,
                                     inode.direct_blocks[direct_index],
                                     &used_data_block_count)) {
        return false;
      }
    }

    if (inode.block_count > kOs64FsDirectBlockCount) {
      if (!mark_data_block_allocated(filesystem, seen_data_blocks,
                                     filesystem->data_bitmap_bytes,
                                     inode.indirect_block,
                                     &used_data_block_count)) {
        return false;
      }

      for (uint32_t file_block_index = kOs64FsDirectBlockCount;
           file_block_index < inode.block_count;
           ++file_block_index) {
        uint32_t block_index = 0;
        if (!inode_block_index_for_file_block(filesystem, &inode,
                                              file_block_index,
                                              &block_index) ||
            !mark_data_block_allocated(filesystem, seen_data_blocks,
                                       filesystem->data_bitmap_bytes,
                                       block_index,
                                       &used_data_block_count)) {
          return false;
        }
      }
    }
  }

  for (uint32_t block_index = 0; block_index < total_data_blocks; ++block_index) {
    const bool bitmap_says_allocated =
        bitmap_bit_is_set(filesystem->data_bitmap_cache,
                          filesystem->data_bitmap_bytes, block_index);
    const bool actually_seen =
        bitmap_bit_is_set(seen_data_blocks, filesystem->data_bitmap_bytes,
                          block_index);
    if (bitmap_says_allocated != actually_seen) {
      set_validation_debug(filesystem, kOs64FsValidationDebugBitmapMismatch,
                           block_index,
                           actually_seen ? 1u : 0u,
                           bitmap_says_allocated ? 1u : 0u);
      return false;
    }
  }

  const uint32_t allocatable_inodes = filesystem->superblock.inode_count - 1;
  const uint32_t free_inode_count =
      allocatable_inodes - used_inode_count;
  const uint32_t free_data_block_count =
      total_data_blocks - used_data_block_count;
  if (filesystem->superblock.free_inode_count != free_inode_count ||
      filesystem->superblock.free_data_block_count != free_data_block_count) {
    set_validation_debug(filesystem, kOs64FsValidationDebugFreeCountMismatch,
                         filesystem->superblock.free_inode_count,
                         free_inode_count,
                         filesystem->superblock.free_data_block_count);
    return false;
  }

  filesystem->stats.total_inodes = filesystem->superblock.inode_count;
  filesystem->stats.allocatable_inodes = allocatable_inodes;
  filesystem->stats.used_inodes = used_inode_count;
  filesystem->stats.free_inodes = free_inode_count;
  filesystem->stats.total_data_blocks = total_data_blocks;
  filesystem->stats.used_data_blocks = used_data_block_count;
  filesystem->stats.free_data_blocks = free_data_block_count;
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

struct PathMutationTarget {
  Os64FsInode parent_inode;                 // 目标路径的父目录 inode。
  Os64FsInode child_inode;                  // 如果目标已经存在，这里顺手带出它自己的 inode。
  uint32_t child_inode_number;              // 目标现有 inode 号；不存在时保持 0。
  uint32_t child_entry_index;               // 目标在父目录里的目录项下标；不存在时保持 0。
  char leaf_name[kOs64FsDirectoryEntryNameCapacity + 1];
                                            // 路径最后一个名字，已经单独拆出来，后面 create/unlink 都直接复用。
  size_t leaf_name_length;
  bool child_exists;
};

void refresh_runtime_stats_from_superblock(Os64Fs* filesystem) {
  if (filesystem == nullptr) {
    return;
  }

  filesystem->stats.free_inodes = filesystem->superblock.free_inode_count;
  filesystem->stats.used_inodes =
      filesystem->stats.allocatable_inodes - filesystem->stats.free_inodes;
  filesystem->stats.free_data_blocks =
      filesystem->superblock.free_data_block_count;
  filesystem->stats.used_data_blocks =
      filesystem->stats.total_data_blocks - filesystem->stats.free_data_blocks;
}

uint32_t block_count_for_size(const Os64Fs* filesystem, uint32_t size_bytes) {
  if (filesystem == nullptr || filesystem->superblock.data_block_size == 0 ||
      size_bytes == 0) {
    return 0;
  }

  return ((size_bytes - 1) / filesystem->superblock.data_block_size) + 1;
}

Os64FsInode* mutable_cached_inode_pointer(Os64Fs* filesystem,
                                         uint32_t inode_number) {
  if (filesystem == nullptr ||
      inode_number >= filesystem->superblock.inode_count) {
    return nullptr;
  }

  const uint32_t inode_offset =
      inode_number * filesystem->superblock.inode_size;
  const uint32_t inode_end = inode_offset + filesystem->superblock.inode_size;
  if (inode_end > filesystem->inode_table_bytes) {
    return nullptr;
  }

  return reinterpret_cast<Os64FsInode*>(
      filesystem->inode_table_cache + inode_offset);
}

void zero_inode_bytes(Os64FsInode* inode) {
  if (inode == nullptr) {
    return;
  }

  memory_set(inode, 0, sizeof(*inode));
}

void initialize_empty_inode_layout(Os64FsInode* inode) {
  if (inode == nullptr) {
    return;
  }

  zero_inode_bytes(inode);
  for (size_t i = 0; i < kOs64FsDirectBlockCount; ++i) {
    inode->direct_blocks[i] = kOs64FsInvalidBlockIndex;
  }
  inode->indirect_block = kOs64FsInvalidBlockIndex;
}

bool write_inode_to_cache(Os64Fs* filesystem, const Os64FsInode* inode) {
  if (filesystem == nullptr || inode == nullptr ||
      inode->inode_number >= filesystem->superblock.inode_count) {
    return false;
  }

  Os64FsInode* const cached_inode =
      mutable_cached_inode_pointer(filesystem, inode->inode_number);
  if (cached_inode == nullptr) {
    return false;
  }

  memory_copy(cached_inode, inode, sizeof(*inode));
  return true;
}

bool split_parent_path_and_name(const char* path,
                                char* out_parent_path,
                                size_t parent_capacity,
                                char* out_leaf_name,
                                size_t leaf_capacity,
                                size_t* out_leaf_name_length) {
  if (path == nullptr || out_parent_path == nullptr ||
      out_leaf_name == nullptr || out_leaf_name_length == nullptr ||
      parent_capacity < 2 || leaf_capacity == 0) {
    return false;
  }

  size_t path_length = 0;
  while (path[path_length] != '\0') {
    ++path_length;
  }

  while (path_length > 0 && is_path_separator(path[path_length - 1])) {
    --path_length;
  }
  if (path_length == 0) {
    return false;
  }

  size_t leaf_begin = path_length;
  while (leaf_begin > 0 && !is_path_separator(path[leaf_begin - 1])) {
    --leaf_begin;
  }

  const size_t leaf_length = path_length - leaf_begin;
  if (leaf_length == 0 ||
      leaf_length > kOs64FsDirectoryEntryNameCapacity ||
      leaf_length >= leaf_capacity) {
    return false;
  }

  for (size_t i = 0; i < leaf_length; ++i) {
    out_leaf_name[i] = path[leaf_begin + i];
  }
  out_leaf_name[leaf_length] = '\0';
  *out_leaf_name_length = leaf_length;

  if (component_is_dot(out_leaf_name, leaf_length) ||
      component_is_dot_dot(out_leaf_name, leaf_length)) {
    return false;
  }

  size_t parent_length = leaf_begin;
  while (parent_length > 0 && is_path_separator(path[parent_length - 1])) {
    --parent_length;
  }

  if (parent_length == 0) {
    out_parent_path[0] = '/';
    out_parent_path[1] = '\0';
    return true;
  }

  if (parent_length >= parent_capacity) {
    return false;
  }

  for (size_t i = 0; i < parent_length; ++i) {
    out_parent_path[i] = path[i];
  }
  out_parent_path[parent_length] = '\0';
  return true;
}

bool lookup_mutation_target(Os64Fs* filesystem,
                            const char* path,
                            PathMutationTarget* out_target) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr ||
      out_target == nullptr) {
    return false;
  }

  memory_set(out_target, 0, sizeof(*out_target));

  char parent_path[kOs64FsMaxPathDepth * (kOs64FsDirectoryEntryNameCapacity + 1)];
  if (!split_parent_path_and_name(path, parent_path, sizeof(parent_path),
                                  out_target->leaf_name,
                                  sizeof(out_target->leaf_name),
                                  &out_target->leaf_name_length) ||
      !os64fs_lookup_path(filesystem, parent_path, &out_target->parent_inode) ||
      out_target->parent_inode.type != kOs64FsTypeDirectory) {
    return false;
  }

  const uint32_t entry_count =
      os64fs_directory_entry_count(filesystem, &out_target->parent_inode);
  for (uint32_t entry_index = 0; entry_index < entry_count; ++entry_index) {
    Os64FsDirEntry entry;
    if (!os64fs_read_directory_entry(filesystem, &out_target->parent_inode,
                                     entry_index, &entry)) {
      return false;
    }

    if (!name_matches(&entry, out_target->leaf_name,
                      out_target->leaf_name_length)) {
      continue;
    }

    out_target->child_exists = true;
    out_target->child_inode_number = entry.inode_number;
    out_target->child_entry_index = entry_index;
    return os64fs_read_inode(filesystem, entry.inode_number,
                             &out_target->child_inode);
  }

  return true;
}

bool write_data_block_bytes(Os64Fs* filesystem,
                            uint32_t block_index,
                            uint32_t block_offset,
                            const void* buffer,
                            size_t bytes_to_write) {
  if (!data_block_index_is_valid(filesystem, block_index) ||
      buffer == nullptr ||
      block_offset > filesystem->superblock.data_block_size ||
      bytes_to_write >
          (filesystem->superblock.data_block_size - block_offset)) {
    return false;
  }

  if (bytes_to_write == 0) {
    return true;
  }

  const uint32_t sector_size = filesystem->device->sector_size;
  const auto* source = static_cast<const uint8_t*>(buffer);
  size_t written = 0;
  while (written < bytes_to_write) {
    const uint32_t absolute_block_offset =
        block_offset + static_cast<uint32_t>(written);
    const uint32_t sector_index =
        filesystem->superblock.data_start_sector +
        (block_index * filesystem->superblock.data_block_size +
         absolute_block_offset) / sector_size;
    const uint32_t sector_offset =
        (block_index * filesystem->superblock.data_block_size +
         absolute_block_offset) % sector_size;

    uint32_t bytes_this_round = sector_size - sector_offset;
    const size_t remaining = bytes_to_write - written;
    if (bytes_this_round > remaining) {
      bytes_this_round = static_cast<uint32_t>(remaining);
    }

    uint8_t sector_buffer[512];
    if (sector_offset != 0 || bytes_this_round != sector_size) {
      if (!block_device_read_sector(filesystem->device, sector_index,
                                    sector_buffer, sizeof(sector_buffer))) {
        return false;
      }
    } else {
      memory_set(sector_buffer, 0, sizeof(sector_buffer));
    }

    memory_copy(sector_buffer + sector_offset, source + written,
                bytes_this_round);
    if (!block_device_write_sector(filesystem->device, sector_index,
                                   sector_buffer, sizeof(sector_buffer))) {
      return false;
    }

    written += bytes_this_round;
  }

  return true;
}

bool zero_data_block(Os64Fs* filesystem, uint32_t block_index) {
  if (!data_block_index_is_valid(filesystem, block_index)) {
    return false;
  }

  uint8_t zero_sector[512];
  memory_set(zero_sector, 0, sizeof(zero_sector));

  const uint32_t sector_size = filesystem->device->sector_size;
  const uint32_t sectors_per_block =
      filesystem->superblock.data_block_size / sector_size;
  for (uint32_t sector_offset = 0; sector_offset < sectors_per_block;
       ++sector_offset) {
    const uint32_t sector_index =
        filesystem->superblock.data_start_sector +
        block_index * sectors_per_block + sector_offset;
    if (!block_device_write_sector(filesystem->device, sector_index,
                                   zero_sector, sizeof(zero_sector))) {
      return false;
    }
  }

  return true;
}

bool write_u32_to_data_block(Os64Fs* filesystem,
                             uint32_t block_index,
                             uint32_t byte_offset_in_block,
                             uint32_t value) {
  return write_data_block_bytes(filesystem, block_index,
                                byte_offset_in_block,
                                &value, sizeof(value));
}

bool sync_metadata(Os64Fs* filesystem) {
  return filesystem != nullptr &&
         write_inode_bitmap(filesystem) &&
         write_data_bitmap(filesystem) &&
         write_inode_table(filesystem) &&
         write_superblock(filesystem);
}

bool allocate_inode_number(Os64Fs* filesystem, uint32_t* out_inode_number) {
  if (!os64fs_is_mounted(filesystem) || out_inode_number == nullptr ||
      filesystem->superblock.free_inode_count == 0) {
    return false;
  }

  for (uint32_t inode_number = 1;
       inode_number < filesystem->superblock.inode_count;
       ++inode_number) {
    if (bitmap_bit_is_set(filesystem->inode_bitmap_cache,
                          filesystem->inode_bitmap_bytes, inode_number)) {
      continue;
    }

    bitmap_set_bit(filesystem->inode_bitmap_cache,
                   filesystem->inode_bitmap_bytes, inode_number);
    --filesystem->superblock.free_inode_count;
    refresh_runtime_stats_from_superblock(filesystem);
    *out_inode_number = inode_number;
    return true;
  }

  return false;
}

bool free_inode_number(Os64Fs* filesystem, uint32_t inode_number) {
  if (!os64fs_is_mounted(filesystem) ||
      inode_number == 0 ||
      inode_number >= filesystem->superblock.inode_count ||
      !bitmap_bit_is_set(filesystem->inode_bitmap_cache,
                         filesystem->inode_bitmap_bytes, inode_number)) {
    return false;
  }

  bitmap_clear_bit(filesystem->inode_bitmap_cache,
                   filesystem->inode_bitmap_bytes, inode_number);
  ++filesystem->superblock.free_inode_count;
  refresh_runtime_stats_from_superblock(filesystem);
  return true;
}

bool allocate_data_block_index(Os64Fs* filesystem, uint32_t* out_block_index) {
  if (!os64fs_is_mounted(filesystem) || out_block_index == nullptr ||
      filesystem->superblock.free_data_block_count == 0) {
    return false;
  }

  const uint32_t total_data_blocks = filesystem_data_block_count(filesystem);
  for (uint32_t block_index = 0; block_index < total_data_blocks; ++block_index) {
    if (bitmap_bit_is_set(filesystem->data_bitmap_cache,
                          filesystem->data_bitmap_bytes, block_index)) {
      continue;
    }

    bitmap_set_bit(filesystem->data_bitmap_cache,
                   filesystem->data_bitmap_bytes, block_index);
    --filesystem->superblock.free_data_block_count;
    refresh_runtime_stats_from_superblock(filesystem);
    if (!zero_data_block(filesystem, block_index)) {
      bitmap_clear_bit(filesystem->data_bitmap_cache,
                       filesystem->data_bitmap_bytes, block_index);
      ++filesystem->superblock.free_data_block_count;
      refresh_runtime_stats_from_superblock(filesystem);
      return false;
    }

    *out_block_index = block_index;
    return true;
  }

  return false;
}

bool free_data_block_index(Os64Fs* filesystem, uint32_t block_index) {
  if (!os64fs_is_mounted(filesystem) ||
      !data_block_index_is_valid(filesystem, block_index) ||
      !bitmap_bit_is_set(filesystem->data_bitmap_cache,
                         filesystem->data_bitmap_bytes, block_index)) {
    return false;
  }

  bitmap_clear_bit(filesystem->data_bitmap_cache,
                   filesystem->data_bitmap_bytes, block_index);
  ++filesystem->superblock.free_data_block_count;
  refresh_runtime_stats_from_superblock(filesystem);
  return true;
}

bool ensure_indirect_block_initialized(Os64Fs* filesystem, Os64FsInode* inode) {
  if (filesystem == nullptr || inode == nullptr) {
    return false;
  }

  if (data_block_index_is_valid(filesystem, inode->indirect_block)) {
    return true;
  }

  uint32_t indirect_block_index = 0;
  if (!allocate_data_block_index(filesystem, &indirect_block_index)) {
    return false;
  }

  const uint32_t entry_capacity =
      filesystem_indirect_entry_capacity(filesystem);
  for (uint32_t entry_index = 0; entry_index < entry_capacity; ++entry_index) {
    if (!write_u32_to_data_block(filesystem, indirect_block_index,
                                 entry_index * sizeof(uint32_t),
                                 kOs64FsInvalidBlockIndex)) {
      (void)free_data_block_index(filesystem, indirect_block_index);
      return false;
    }
  }

  inode->indirect_block = indirect_block_index;
  return true;
}

bool ensure_inode_file_block(Os64Fs* filesystem,
                             Os64FsInode* inode,
                             uint32_t file_block_index,
                             uint32_t* out_block_index) {
  if (filesystem == nullptr || inode == nullptr || out_block_index == nullptr) {
    return false;
  }

  if (file_block_index < inode->block_count) {
    return inode_block_index_for_file_block(filesystem, inode,
                                            file_block_index,
                                            out_block_index);
  }

  if (file_block_index != inode->block_count) {
    return false;
  }

  uint32_t new_block_index = 0;
  if (!allocate_data_block_index(filesystem, &new_block_index)) {
    return false;
  }

  if (file_block_index < kOs64FsDirectBlockCount) {
    inode->direct_blocks[file_block_index] = new_block_index;
  } else {
    const uint32_t indirect_slot =
        file_block_index - static_cast<uint32_t>(kOs64FsDirectBlockCount);
    if (indirect_slot >= filesystem_indirect_entry_capacity(filesystem) ||
        !ensure_indirect_block_initialized(filesystem, inode) ||
        !write_u32_to_data_block(filesystem, inode->indirect_block,
                                 indirect_slot * sizeof(uint32_t),
                                 new_block_index)) {
      (void)free_data_block_index(filesystem, new_block_index);
      return false;
    }
  }

  ++inode->block_count;
  *out_block_index = new_block_index;
  return true;
}

bool free_inode_file_block(Os64Fs* filesystem,
                           Os64FsInode* inode,
                           uint32_t file_block_index) {
  if (filesystem == nullptr || inode == nullptr ||
      file_block_index >= inode->block_count) {
    return false;
  }

  if (file_block_index < kOs64FsDirectBlockCount) {
    const uint32_t block_index = inode->direct_blocks[file_block_index];
    if (!free_data_block_index(filesystem, block_index)) {
      return false;
    }

    inode->direct_blocks[file_block_index] = kOs64FsInvalidBlockIndex;
    return true;
  }

  const uint32_t indirect_slot =
      file_block_index - static_cast<uint32_t>(kOs64FsDirectBlockCount);
  uint32_t block_index = 0;
  if (!read_u32_from_data_block(filesystem, inode->indirect_block,
                                indirect_slot * sizeof(uint32_t),
                                &block_index) ||
      !free_data_block_index(filesystem, block_index) ||
      !write_u32_to_data_block(filesystem, inode->indirect_block,
                               indirect_slot * sizeof(uint32_t),
                               kOs64FsInvalidBlockIndex)) {
    return false;
  }

  return true;
}

bool truncate_inode_to_size(Os64Fs* filesystem,
                            Os64FsInode* inode,
                            uint32_t new_size) {
  if (filesystem == nullptr || inode == nullptr || new_size > inode->size_bytes) {
    return false;
  }

  const uint32_t required_blocks = block_count_for_size(filesystem, new_size);
  while (inode->block_count > required_blocks) {
    const uint32_t file_block_index = inode->block_count - 1;
    if (!free_inode_file_block(filesystem, inode, file_block_index)) {
      return false;
    }

    --inode->block_count;
  }

  if (inode->block_count <= kOs64FsDirectBlockCount &&
      data_block_index_is_valid(filesystem, inode->indirect_block)) {
    if (!free_data_block_index(filesystem, inode->indirect_block)) {
      return false;
    }

    inode->indirect_block = kOs64FsInvalidBlockIndex;
  }

  inode->size_bytes = new_size;
  return true;
}

bool write_inode_bytes(Os64Fs* filesystem,
                       Os64FsInode* inode,
                       uint32_t offset,
                       const void* buffer,
                       size_t bytes_to_write) {
  if (filesystem == nullptr || inode == nullptr || buffer == nullptr ||
      offset > inode->size_bytes) {
    return false;
  }

  if (bytes_to_write == 0) {
    return true;
  }

  const uint32_t block_size = filesystem->superblock.data_block_size;
  const auto* source = static_cast<const uint8_t*>(buffer);
  size_t written = 0;
  while (written < bytes_to_write) {
    const uint32_t absolute_offset = offset + static_cast<uint32_t>(written);
    const uint32_t file_block_index = absolute_offset / block_size;
    const uint32_t block_offset = absolute_offset % block_size;

    uint32_t block_index = 0;
    if (!ensure_inode_file_block(filesystem, inode, file_block_index,
                                 &block_index)) {
      return false;
    }

    uint32_t bytes_this_round = block_size - block_offset;
    const size_t remaining = bytes_to_write - written;
    if (bytes_this_round > remaining) {
      bytes_this_round = static_cast<uint32_t>(remaining);
    }

    if (!write_data_block_bytes(filesystem, block_index, block_offset,
                                source + written, bytes_this_round)) {
      return false;
    }

    written += bytes_this_round;
  }

  const uint32_t new_end = offset + static_cast<uint32_t>(bytes_to_write);
  if (new_end > inode->size_bytes) {
    inode->size_bytes = new_end;
  }

  return true;
}

bool append_directory_entry(Os64Fs* filesystem,
                            Os64FsInode* directory_inode,
                            const Os64FsDirEntry* entry) {
  return filesystem != nullptr &&
         directory_inode != nullptr &&
         entry != nullptr &&
         write_inode_bytes(filesystem, directory_inode,
                           directory_inode->size_bytes,
                           entry, sizeof(*entry));
}

bool remove_directory_entry(Os64Fs* filesystem,
                            Os64FsInode* directory_inode,
                            uint32_t entry_index) {
  if (filesystem == nullptr || directory_inode == nullptr) {
    return false;
  }

  const uint32_t entry_count =
      os64fs_directory_entry_count(filesystem, directory_inode);
  if (entry_index >= entry_count) {
    return false;
  }

  if (entry_index + 1 < entry_count) {
    Os64FsDirEntry last_entry;
    if (!os64fs_read_directory_entry(filesystem, directory_inode,
                                     entry_count - 1, &last_entry) ||
        !write_inode_bytes(filesystem, directory_inode,
                           entry_index * sizeof(Os64FsDirEntry),
                           &last_entry, sizeof(last_entry))) {
      return false;
    }
  }

  const uint32_t new_size =
      directory_inode->size_bytes - sizeof(Os64FsDirEntry);
  return truncate_inode_to_size(filesystem, directory_inode, new_size);
}

bool initialize_new_inode(Os64FsInode* inode,
                          uint32_t inode_number,
                          uint16_t type) {
  if (inode == nullptr) {
    return false;
  }

  initialize_empty_inode_layout(inode);
  inode->inode_number = inode_number;
  inode->type = type;
  inode->link_count = 1;
  inode->mode = (type == kOs64FsTypeDirectory) ? 493 : 420;
  return true;
}

}  // namespace

bool initialize_os64fs(Os64Fs* filesystem, BlockDevice* device) {
  if (filesystem == nullptr || !block_device_is_ready(device)) {
    if (filesystem != nullptr) {
      memory_set(filesystem, 0, sizeof(*filesystem));
      filesystem->mount_error = kOs64FsMountBadDevice;
    }
    return false;
  }

  memory_set(filesystem, 0, sizeof(*filesystem));
  filesystem->device = device;

  if (!read_superblock(filesystem)) {
    filesystem->mount_error = kOs64FsMountReadSuperblockFailed;
    filesystem->device = nullptr;
    return false;
  }

  if (!filesystem_layout_is_valid(filesystem)) {
    filesystem->mount_error = kOs64FsMountLayoutInvalid;
    filesystem->device = nullptr;
    return false;
  }

  if (!read_bitmap(filesystem,
                   filesystem->superblock.inode_bitmap_start_sector,
                   filesystem->superblock.inode_bitmap_sector_count,
                   bitmap_required_bytes(filesystem->superblock.inode_count),
                   filesystem->inode_bitmap_cache,
                   sizeof(filesystem->inode_bitmap_cache),
                   &filesystem->inode_bitmap_bytes)) {
    filesystem->mount_error = kOs64FsMountReadInodeBitmapFailed;
    filesystem->device = nullptr;
    return false;
  }

  if (!read_bitmap(filesystem,
                   filesystem->superblock.data_bitmap_start_sector,
                   filesystem->superblock.data_bitmap_sector_count,
                   bitmap_required_bytes(filesystem_data_block_count(filesystem)),
                   filesystem->data_bitmap_cache,
                   sizeof(filesystem->data_bitmap_cache),
                   &filesystem->data_bitmap_bytes)) {
    filesystem->mount_error = kOs64FsMountReadDataBitmapFailed;
    filesystem->device = nullptr;
    return false;
  }

  if (!read_inode_table(filesystem)) {
    filesystem->mount_error = kOs64FsMountReadInodeTableFailed;
    filesystem->device = nullptr;
    return false;
  }

  filesystem->mounted = true;

  Os64FsInode root_inode;
  if (!os64fs_read_inode(filesystem, filesystem->superblock.root_inode,
                         &root_inode) ||
      root_inode.type != kOs64FsTypeDirectory) {
    filesystem->mount_error = kOs64FsMountRootInodeInvalid;
    filesystem->mounted = false;
    filesystem->device = nullptr;
    return false;
  }

  if (!validate_allocation_maps_and_collect_stats(filesystem)) {
    filesystem->mount_error = kOs64FsMountAllocationMismatch;
    filesystem->mounted = false;
    filesystem->device = nullptr;
    return false;
  }

  filesystem->mount_error = kOs64FsMountOk;
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

bool os64fs_query_stats(const Os64Fs* filesystem, Os64FsStats* out_stats) {
  if (!os64fs_is_mounted(filesystem) || out_stats == nullptr) {
    return false;
  }

  memory_copy(out_stats, &filesystem->stats, sizeof(*out_stats));
  return true;
}

bool os64fs_query_validation_debug(const Os64Fs* filesystem,
                                   Os64FsValidationDebug* out_debug) {
  if (filesystem == nullptr || out_debug == nullptr) {
    return false;
  }

  memory_copy(out_debug, &filesystem->validation_debug, sizeof(*out_debug));
  return true;
}

uint32_t os64fs_mount_error(const Os64Fs* filesystem) {
  return filesystem != nullptr ? filesystem->mount_error : kOs64FsMountBadDevice;
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
  if (inode_end > filesystem->inode_table_bytes) {
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

  uint32_t inode_stack[kOs64FsMaxPathDepth];
  size_t depth = 1;
  inode_stack[0] = filesystem->superblock.root_inode;

  Os64FsInode current_inode;
  if (!os64fs_read_inode(filesystem, inode_stack[0], &current_inode)) {
    return false;
  }

  const char* cursor = skip_path_separators(path);
  if (cursor == nullptr || cursor[0] == '\0') {
    memory_copy(out_inode, &current_inode, sizeof(current_inode));
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

    if (depth >= kOs64FsMaxPathDepth) {
      return false;
    }

    inode_stack[depth++] = next_inode_number;

    cursor = skip_path_separators(cursor + current_component_length);
  }

  memory_copy(out_inode, &current_inode, sizeof(current_inode));
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

bool os64fs_create_file(Os64Fs* filesystem, const char* path) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  PathMutationTarget target;
  if (!lookup_mutation_target(filesystem, path, &target) ||
      target.child_exists) {
    return false;
  }

  uint32_t inode_number = 0;
  if (!allocate_inode_number(filesystem, &inode_number)) {
    return false;
  }

  Os64FsInode child_inode;
  if (!initialize_new_inode(&child_inode, inode_number, kOs64FsTypeFile)) {
    (void)free_inode_number(filesystem, inode_number);
    return false;
  }

  Os64FsDirEntry entry;
  memory_set(&entry, 0, sizeof(entry));
  entry.inode_number = inode_number;
  entry.type = kOs64FsTypeFile;
  entry.name_length = static_cast<uint8_t>(target.leaf_name_length);
  for (size_t i = 0; i < target.leaf_name_length; ++i) {
    entry.name[i] = target.leaf_name[i];
  }

  if (!write_inode_to_cache(filesystem, &child_inode)) {
    (void)free_inode_number(filesystem, inode_number);
    return false;
  }

  if (!append_directory_entry(filesystem, &target.parent_inode, &entry) ||
      !write_inode_to_cache(filesystem, &target.parent_inode) ||
      !sync_metadata(filesystem)) {
    return false;
  }

  return true;
}

bool os64fs_create_directory(Os64Fs* filesystem, const char* path) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  PathMutationTarget target;
  if (!lookup_mutation_target(filesystem, path, &target) ||
      target.child_exists) {
    return false;
  }

  uint32_t inode_number = 0;
  if (!allocate_inode_number(filesystem, &inode_number)) {
    return false;
  }

  Os64FsInode child_inode;
  if (!initialize_new_inode(&child_inode, inode_number,
                            kOs64FsTypeDirectory) ||
      !write_inode_to_cache(filesystem, &child_inode)) {
    (void)free_inode_number(filesystem, inode_number);
    return false;
  }

  Os64FsDirEntry entry;
  memory_set(&entry, 0, sizeof(entry));
  entry.inode_number = inode_number;
  entry.type = kOs64FsTypeDirectory;
  entry.name_length = static_cast<uint8_t>(target.leaf_name_length);
  for (size_t i = 0; i < target.leaf_name_length; ++i) {
    entry.name[i] = target.leaf_name[i];
  }

  if (!append_directory_entry(filesystem, &target.parent_inode, &entry) ||
      !write_inode_to_cache(filesystem, &target.parent_inode) ||
      !sync_metadata(filesystem)) {
    return false;
  }

  return true;
}

namespace {

bool write_file_common(Os64Fs* filesystem,
                       const char* path,
                       const void* buffer,
                       size_t bytes_to_write,
                       bool append_mode) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  PathMutationTarget target;
  if (!lookup_mutation_target(filesystem, path, &target)) {
    return false;
  }

  Os64FsInode file_inode;
  if (!target.child_exists) {
    if (!os64fs_create_file(filesystem, path) ||
        !os64fs_read_inode(filesystem, target.parent_inode.inode_number,
                           &target.parent_inode) ||
        !lookup_mutation_target(filesystem, path, &target) ||
        !target.child_exists) {
      return false;
    }
  }

  memory_copy(&file_inode, &target.child_inode, sizeof(file_inode));
  if (file_inode.type != kOs64FsTypeFile) {
    return false;
  }

  const uint32_t write_offset = append_mode ? file_inode.size_bytes : 0;
  if (!append_mode && !truncate_inode_to_size(filesystem, &file_inode, 0)) {
    return false;
  }

  if (bytes_to_write > 0 &&
      (buffer == nullptr ||
       !write_inode_bytes(filesystem, &file_inode, write_offset,
                          buffer, bytes_to_write))) {
    return false;
  }

  if (!write_inode_to_cache(filesystem, &file_inode) ||
      !sync_metadata(filesystem)) {
    return false;
  }

  return true;
}

}  // namespace

bool os64fs_write_file(Os64Fs* filesystem, const char* path,
                       const void* buffer, size_t bytes_to_write) {
  return write_file_common(filesystem, path, buffer, bytes_to_write, false);
}

bool os64fs_append_file(Os64Fs* filesystem, const char* path,
                        const void* buffer, size_t bytes_to_write) {
  return write_file_common(filesystem, path, buffer, bytes_to_write, true);
}

bool os64fs_unlink(Os64Fs* filesystem, const char* path) {
  if (!os64fs_is_mounted(filesystem) || path == nullptr) {
    return false;
  }

  PathMutationTarget target;
  if (!lookup_mutation_target(filesystem, path, &target) ||
      !target.child_exists) {
    return false;
  }

  Os64FsInode child_inode;
  memory_copy(&child_inode, &target.child_inode, sizeof(child_inode));
  if (child_inode.type == kOs64FsTypeDirectory &&
      os64fs_directory_entry_count(filesystem, &child_inode) != 0) {
    return false;
  }

  if (!truncate_inode_to_size(filesystem, &child_inode, 0) ||
      !write_inode_to_cache(filesystem, &child_inode) ||
      !remove_directory_entry(filesystem, &target.parent_inode,
                              target.child_entry_index) ||
      !write_inode_to_cache(filesystem, &target.parent_inode)) {
    return false;
  }

  Os64FsInode* const cached_child_inode =
      mutable_cached_inode_pointer(filesystem, child_inode.inode_number);
  if (cached_child_inode == nullptr ||
      !free_inode_number(filesystem, child_inode.inode_number)) {
    return false;
  }

  zero_inode_bytes(cached_child_inode);
  return sync_metadata(filesystem);
}

bool os64fs_sync(Os64Fs* filesystem) {
  return os64fs_is_mounted(filesystem) && sync_metadata(filesystem);
}
