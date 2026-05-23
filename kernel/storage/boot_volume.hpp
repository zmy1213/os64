#ifndef OS64_BOOT_VOLUME_HPP
#define OS64_BOOT_VOLUME_HPP

#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"

// BootVolume 表示：
// “stage2 已经帮 64 位内核预读进内存的一小段原始启动介质”。
//
// 你可以把它先理解成：
// 现在我们还不会自己写 ATA / AHCI 驱动去真的访问磁盘，
// 但 stage2 已经提前把一段连续扇区搬进 RAM 了，
// 所以内核可以先把这段 RAM 假装成“一个最小可读写磁盘卷”来练文件系统。
struct BootVolume {
  uint8_t* base;                  // stage2 已经把这段连续扇区搬进内存，所以这里就是它的起始地址；现在允许文件系统把修改写回这段内存卷。
  uint32_t start_lba;             // 它在原始磁盘镜像里的起始 LBA。
  uint16_t sector_count;          // 一共预读了多少个扇区。
  uint16_t sector_size;           // 这一轮仍然固定成 512 字节。
  bool ready;                     // 初始化成功以后，上层才能把它当成原始块设备数据来读。
};

// 根据 stage2 传进来的 BootInfo 初始化 BootVolume。
// `volume` 是输出对象，函数会先把它清空再写入。
// `boot_info` 是 stage2 留给内核的启动说明书，里面记录了 boot volume 在内存中的位置和大小。
// 返回值：
// - true  表示这段预读卷信息合法，现在可以按扇区访问它
// - false 表示 BootInfo 不完整，或者根本没有这段卷
bool initialize_boot_volume(BootVolume* volume, const BootInfo* boot_info);

// 判断一个 BootVolume 是否真的可用。
// 这是上层最常见的保护检查，避免在空指针或未初始化对象上继续读扇区。
bool boot_volume_is_ready(const BootVolume* volume);

// 返回整段 boot volume 一共有多少字节。
// 本质上就是 `sector_count * sector_size`，只是统一封装成接口，避免每层都自己算。
uint64_t boot_volume_total_bytes(const BootVolume* volume);

// 读取一个扇区。
// `sector_index` 是相对于这段 boot volume 自己的扇区下标，不是整张磁盘的全局 LBA。
// `buffer` 是调用者准备好的输出缓冲区。
// `buffer_size` 至少要 >= `sector_size`，否则这一扇区没地方装。
bool boot_volume_read_sector(const BootVolume* volume, uint32_t sector_index,
                             void* buffer, size_t buffer_size);

// 写回一个扇区。
// 现在写到的不是“真正的物理磁盘”，而是写回这段内存里的 boot volume 副本。
// 这样文件系统可以先练“可写卷”逻辑，而不必一开始就碰真实磁盘控制器。
bool boot_volume_write_sector(BootVolume* volume, uint32_t sector_index,
                              const void* buffer, size_t buffer_size);

#endif
