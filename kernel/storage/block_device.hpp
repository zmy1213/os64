#ifndef OS64_BLOCK_DEVICE_HPP
#define OS64_BLOCK_DEVICE_HPP

#include <stddef.h>
#include <stdint.h>

#include "storage/boot_volume.hpp"

// 这一层的目标是把“底层数据来源是什么”抽象掉。
// 这样文件系统以后只需要面对“按扇区读一个块设备”，
// 而不用知道数据到底来自 stage2 预读内存、ATA、AHCI，还是别的驱动。
struct BlockDevice {
  void* context;          // 真正的数据来源对象，这一轮会指向 BootVolume；写路径也会复用同一个上下文对象。
  uint32_t start_lba;     // 这个块设备在原始介质里的起始 LBA。
  uint32_t sector_count;  // 这个块设备一共有多少个扇区。
  uint16_t sector_size;   // 每个扇区大小，当前仍然是 512 字节。
  bool ready;             // 只有初始化成功后，文件系统才允许挂载它。

  // 这是块设备最核心的能力：按扇区读。
  bool (*read_sector)(const void* context,
                      uint32_t sector_index,
                      void* buffer,
                      size_t buffer_size);
  bool (*write_sector)(void* context,
                       uint32_t sector_index,
                       const void* buffer,
                       size_t buffer_size);
};

// 把一段已经准备好的 BootVolume 包装成统一的块设备接口。
// 这样上层文件系统以后只面向 BlockDevice，不再关心底下具体是不是 boot volume。
bool initialize_block_device_from_boot_volume(BlockDevice* device,
                                              BootVolume* volume);

// 判断块设备是否完成初始化，且读/写函数指针都已经挂好。
bool block_device_is_ready(const BlockDevice* device);

// 返回整块设备的总字节数。
// 这层和 BootVolume 一样做统一封装，避免上层到处自己乘扇区数。
uint64_t block_device_total_bytes(const BlockDevice* device);

// 读取一个逻辑扇区。
// `sector_index` 仍然是“这个块设备自己的第几个扇区”，不是整盘全局 LBA。
bool block_device_read_sector(const BlockDevice* device, uint32_t sector_index,
                              void* buffer, size_t buffer_size);

// 写回一个逻辑扇区。
// 当前最终会落到 BootVolume 的内存副本；以后换成 ATA/AHCI 驱动时，这个接口可以保持不变。
bool block_device_write_sector(BlockDevice* device, uint32_t sector_index,
                               const void* buffer, size_t buffer_size);

#endif
