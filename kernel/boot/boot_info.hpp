#ifndef OS64_BOOT_INFO_HPP
#define OS64_BOOT_INFO_HPP

#include <stdint.h>

// 这份结构体就是 stage2 交给 64 位内核的“最小启动说明书”。
// 现在除了 E820 之外，我们还多传一份“boot volume 已经被预读到哪里”的信息，
// 这样 64 位内核即使还没有真正的磁盘控制器驱动，也能先按扇区读取一段启动介质数据。
constexpr uint32_t kBootInfoMagic = 0x3436534f;  // 按 ASCII 看就是 "OS64"。
constexpr uint32_t kE820TypeUsable = 1;          // E820 里 type=1 通常表示“这段内存可以放心分配”。
constexpr uint16_t kBootVolumeSectorSize = 512;  // 这一轮先固定成传统扇区大小 512 字节。

// 这就是 stage2 在实模式里从 BIOS 收到的那种 24 字节 E820 记录。
// 内核以后会直接按这个布局去读 BootInfo.memory_map_ptr 指向的内容。
struct E820Entry {
  uint64_t base;                  // 这段物理内存从哪个地址开始。
  uint64_t length;                // 这段物理内存一共有多长。
  uint32_t type;                  // 类型：1=usable，其它值大多都要当成不能随便碰的保留区。
  uint32_t extended_attributes;   // 扩展属性字段，这一轮先保留着，后面再深入用。
};

struct BootInfo {
  uint32_t magic;                 // 先检查 magic，确认拿到的是我们约定的结构。
  uint16_t memory_map_count;      // 一共拿到了多少条 E820 记录。
  uint16_t memory_map_entry_size; // 每条 E820 记录现在固定是 24 字节。
  uint64_t memory_map_ptr;        // E820 数组在内存里的起始地址。
  uint64_t boot_volume_ptr;       // stage2 预读到内存里的 boot volume 起始地址。
  uint32_t boot_volume_start_lba; // 这段 boot volume 在原始磁盘镜像里的起始 LBA（从 0 开始）。
  uint16_t boot_volume_sector_count;  // 一共预读了多少个扇区。
  uint16_t boot_volume_sector_size;   // 当前固定是 512，先显式传进来方便后面扩展。
};

static_assert(sizeof(E820Entry) == 24, "E820 entry layout must stay 24 bytes");
static_assert(sizeof(BootInfo) == 32, "BootInfo layout must stay 32 bytes");

#endif
