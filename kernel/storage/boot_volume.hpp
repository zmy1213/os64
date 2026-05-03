#ifndef OS64_BOOT_VOLUME_HPP
#define OS64_BOOT_VOLUME_HPP

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"

struct BootVolume {
  const uint8_t* base;            // stage2 已经把这段连续扇区搬进内存，所以这里就是它的起始地址。
  uint32_t start_lba;             // 它在原始磁盘镜像里的起始 LBA。
  uint16_t sector_count;          // 一共预读了多少个扇区。
  uint16_t sector_size;           // 这一轮仍然固定成 512 字节。
  bool ready;                     // 初始化成功以后，上层才能把它当成原始块设备数据来读。
};

bool initialize_boot_volume(BootVolume* volume, const BootInfo* boot_info);
bool boot_volume_is_ready(const BootVolume* volume);
uint64_t boot_volume_total_bytes(const BootVolume* volume);
bool boot_volume_read_sector(const BootVolume* volume, uint32_t sector_index,
                             void* buffer, size_t buffer_size);

#endif
