#ifndef OS64_BOOT_VOLUME_HPP
#define OS64_BOOT_VOLUME_HPP

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"

// 这是这一步引入的最小“启动卷头”。
// 它不是成熟文件系统，只是一份很小的自描述头部，
// 目的是先把“按扇区读取一段介质数据”这条链打通。
struct BootVolumeHeader {
  char signature[8];              // 固定写成 "OS64VOL1"，方便第一眼确认读到的是我们自己的卷。
  uint32_t version;               // 头格式版本，当前先固定成 1。
  uint32_t total_sectors;         // 整个 boot volume 一共有多少个扇区。
  uint32_t sector_size;           // 每个扇区多大，这一轮固定是 512。
  char volume_name[16];           // 给这段卷起一个简单名字，方便 shell 里打印。
  uint32_t readme_sector_index;   // README 文本所在的相对扇区号。
  uint32_t readme_length;         // README 文本长度，方便后面继续往“小文件读取”走。
  uint32_t notes_sector_index;    // 第二段文本所在的相对扇区号。
  uint32_t notes_length;          // 第二段文本长度。
};

static_assert(sizeof(BootVolumeHeader) == 52,
              "BootVolumeHeader layout must stay 52 bytes");

struct BootVolume {
  const uint8_t* base;                // 这段 boot volume 被 stage2 预读到了哪块内存。
  const BootVolumeHeader* header;     // 第 0 扇区开头就放卷头，所以这里直接指向它。
  uint32_t start_lba;                 // 它在原始磁盘镜像里的起始 LBA。
  uint16_t sector_count;              // 一共有多少个扇区。
  uint16_t sector_size;               // 每个扇区大小。
  bool ready;                         // 只有初始化并通过校验后，才允许上层去读它。
};

bool initialize_boot_volume(BootVolume* volume, const BootInfo* boot_info);
bool boot_volume_is_ready(const BootVolume* volume);
const BootVolumeHeader* boot_volume_header(const BootVolume* volume);
uint32_t boot_volume_total_bytes(const BootVolume* volume);
bool boot_volume_read_sector(const BootVolume* volume, uint32_t sector_index,
                             void* buffer, size_t buffer_size);

#endif
