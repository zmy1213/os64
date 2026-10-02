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

  // 这一层本质是在“填一张虚函数表风格的小对象”：
  // 记录上下文是谁、容量有多大、真正的读写函数应该调谁。
  device->context = volume;
  device->start_lba = volume->start_lba;
  device->sector_count = volume->sector_count;
  device->sector_size = volume->sector_size;
  device->ready = true;
  device->read_sector = read_boot_volume_sector;
  device->write_sector = write_boot_volume_sector;
  device->flush = nullptr;
  return true;
}

bool block_device_is_ready(const BlockDevice* device) {
  return device != nullptr && device->ready &&
         device->sector_size == 512 && device->sector_count != 0 &&
         device->read_sector != nullptr &&
         device->write_sector != nullptr;
}

uint64_t block_device_total_bytes(const BlockDevice* device) {
  if (!block_device_is_ready(device)) {
    return 0;
  }

  // 继续统一用 64 位保存容量，避免接口层把大小截断。
  return static_cast<uint64_t>(device->sector_count) * device->sector_size;
}

bool block_device_read_sector(const BlockDevice* device, uint32_t sector_index,
                              void* buffer, size_t buffer_size) {
  if (!block_device_is_ready(device) || buffer == nullptr ||
      buffer_size < device->sector_size ||
      sector_index >= device->sector_count) {
    return false;
  }

  // 这一步真正体现了“抽象层”的意义：
  // BlockDevice 自己不管底层是什么，只负责把请求转发给挂好的函数指针。
  return device->read_sector(device->context, sector_index,
                             buffer, buffer_size);
}

bool block_device_write_sector(BlockDevice* device, uint32_t sector_index,
                               const void* buffer, size_t buffer_size) {
  if (!block_device_is_ready(device) || buffer == nullptr ||
      buffer_size < device->sector_size ||
      sector_index >= device->sector_count) {
    return false;
  }

  // 写路径同理，继续通过统一接口转发到底层实现。
  return device->write_sector(device->context, sector_index,
                              buffer, buffer_size);
}

bool block_device_flush(BlockDevice* device) {
  return block_device_is_ready(device) &&
         (device->flush == nullptr || device->flush(device->context));
}
