#include "storage/boot_volume.hpp"

#include "runtime/runtime.hpp"

bool initialize_boot_volume(BootVolume* volume, const BootInfo* boot_info) {
  // 先做最基本的空指针保护。
  if (volume == nullptr || boot_info == nullptr) {
    return false;
  }

  // 不管后面是否成功，先把输出对象清成“完全未初始化”状态。
  // 这样调用者即使忽略返回值，也不容易拿到半初始化对象继续用。
  volume->base = nullptr;
  volume->start_lba = 0;
  volume->sector_count = 0;
  volume->sector_size = 0;
  volume->ready = false;

  // BootInfo 里只要下面任一关键字段不合法，
  // 就说明 stage2 没有把这段卷准备好，当前内核不能把它当块设备使用。
  if (boot_info->boot_volume_ptr == 0 ||
      boot_info->boot_volume_sector_count == 0 ||
      boot_info->boot_volume_sector_size != kBootVolumeSectorSize) {
    return false;
  }

  // 这里不再拷贝整段卷内容，而是直接记录 stage2 预读区的地址。
  // 好处是实现简单，而且上层写回时能立刻反映到同一块内存上。
  volume->base = reinterpret_cast<uint8_t*>(
      static_cast<uintptr_t>(boot_info->boot_volume_ptr));
  volume->start_lba = boot_info->boot_volume_start_lba;
  volume->sector_count = boot_info->boot_volume_sector_count;
  volume->sector_size = boot_info->boot_volume_sector_size;
  volume->ready = true;
  return true;
}

bool boot_volume_is_ready(const BootVolume* volume) {
  return volume != nullptr && volume->ready;
}

uint64_t boot_volume_total_bytes(const BootVolume* volume) {
  if (!boot_volume_is_ready(volume)) {
    return 0;
  }

  // 用 64 位返回，避免以后卷变大时 32 位乘法提前溢出。
  return static_cast<uint64_t>(volume->sector_count) * volume->sector_size;
}

bool boot_volume_read_sector(const BootVolume* volume, uint32_t sector_index,
                             void* buffer, size_t buffer_size) {
  if (!boot_volume_is_ready(volume) || buffer == nullptr ||
      buffer_size < volume->sector_size ||
      sector_index >= volume->sector_count) {
    return false;
  }

  // 先把“第几个扇区”换算成“从 base 开始偏移多少字节”。
  const uint32_t byte_offset = sector_index * volume->sector_size;
  // 这一层不解析任何文件系统格式，只做最原始的“按扇区复制字节”。
  memory_copy(buffer, volume->base + byte_offset, volume->sector_size);
  return true;
}

bool boot_volume_write_sector(BootVolume* volume, uint32_t sector_index,
                              const void* buffer, size_t buffer_size) {
  if (!boot_volume_is_ready(volume) || buffer == nullptr ||
      buffer_size < volume->sector_size ||
      sector_index >= volume->sector_count) {
    return false;
  }

  const uint32_t byte_offset = sector_index * volume->sector_size;
  // 写路径和读路径对称：仍然只是把一个扇区大小的数据复制回卷内存。
  memory_copy(volume->base + byte_offset, buffer, volume->sector_size);
  return true;
}
