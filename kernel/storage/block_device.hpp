#ifndef OS64_BLOCK_DEVICE_HPP
#define OS64_BLOCK_DEVICE_HPP

#include <stddef.h>
#include <stdint.h>

#include "storage/boot_volume.hpp"

// 这一层的目标是把“底层数据来源是什么”抽象掉。
// 这样文件系统以后只需要面对“按扇区读一个块设备”，
// 而不用知道数据到底来自 stage2 预读内存、ATA、AHCI，还是别的驱动。
struct BlockDevice {
  const void* context;    // 真正的数据来源对象，这一轮会指向 BootVolume。
  uint32_t start_lba;     // 这个块设备在原始介质里的起始 LBA。
  uint32_t sector_count;  // 这个块设备一共有多少个扇区。
  uint16_t sector_size;   // 每个扇区大小，当前仍然是 512 字节。
  bool ready;             // 只有初始化成功后，文件系统才允许挂载它。

  // 这是块设备最核心的能力：按扇区读。
  bool (*read_sector)(const void* context,
                      uint32_t sector_index,
                      void* buffer,
                      size_t buffer_size);
};

bool initialize_block_device_from_boot_volume(BlockDevice* device,
                                              const BootVolume* volume);
bool block_device_is_ready(const BlockDevice* device);
uint64_t block_device_total_bytes(const BlockDevice* device);
bool block_device_read_sector(const BlockDevice* device, uint32_t sector_index,
                              void* buffer, size_t buffer_size);

#endif
