#include "storage/block_device.hpp"

namespace {

// 这一轮真正的底层读取动作仍然委托给 BootVolume。
// 但对文件系统来说，它已经只是在“读一个块设备扇区”了。
bool read_boot_volume_sector(const void* context,
                             uint32_t sector_index,
                             void* buffer,
                             size_t buffer_size) {
  return boot_volume_read_sector(static_cast<const BootVolume*>(context),
                                 sector_index, buffer, buffer_size);
}

bool write_boot_volume_sector(void* context,
                              uint32_t sector_index,
                              const void* buffer,
                              size_t buffer_size) {
  return boot_volume_write_sector(static_cast<BootVolume*>(context),
                                  sector_index, buffer, buffer_size);
}

}  // namespace

bool initialize_block_device_from_boot_volume(BlockDevice* device,
                                              BootVolume* volume) {
  if (device == nullptr || !boot_volume_is_ready(volume)) {
    return false;
  }

  device->context = volume;
  device->start_lba = volume->start_lba;
  device->sector_count = volume->sector_count;
  device->sector_size = volume->sector_size;
  device->ready = true;
  device->read_sector = read_boot_volume_sector;
  device->write_sector = write_boot_volume_sector;
  return true;
}

bool block_device_is_ready(const BlockDevice* device) {
  return device != nullptr && device->ready &&
         device->read_sector != nullptr &&
         device->write_sector != nullptr;
}

uint64_t block_device_total_bytes(const BlockDevice* device) {
  if (!block_device_is_ready(device)) {
    return 0;
  }

  return static_cast<uint64_t>(device->sector_count) * device->sector_size;
}

bool block_device_read_sector(const BlockDevice* device, uint32_t sector_index,
                              void* buffer, size_t buffer_size) {
  if (!block_device_is_ready(device) || buffer == nullptr ||
      sector_index >= device->sector_count) {
    return false;
  }

  return device->read_sector(device->context, sector_index,
                             buffer, buffer_size);
}

bool block_device_write_sector(BlockDevice* device, uint32_t sector_index,
                               const void* buffer, size_t buffer_size) {
  if (!block_device_is_ready(device) || buffer == nullptr ||
      sector_index >= device->sector_count) {
    return false;
  }

  return device->write_sector(device->context, sector_index,
                              buffer, buffer_size);
}
