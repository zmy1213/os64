#ifndef OS64_BOOT_INFO_HPP
#define OS64_BOOT_INFO_HPP

#include <stdint.h>

// 这份结构体就是 stage2 交给 64 位内核的“最小启动说明书”。
// 现在我们先只传最重要的 E820 信息，后面再慢慢加 framebuffer、ACPI 等内容。
constexpr uint32_t kBootInfoMagic = 0x3436534f;  // 按 ASCII 看就是 "OS64"。

struct BootInfo {
  uint32_t magic;                 // 先检查 magic，确认拿到的是我们约定的结构。
  uint16_t memory_map_count;      // 一共拿到了多少条 E820 记录。
  uint16_t memory_map_entry_size; // 每条 E820 记录现在固定是 24 字节。
  uint64_t memory_map_ptr;        // E820 数组在内存里的起始地址。
};

static_assert(sizeof(BootInfo) == 16, "BootInfo layout must stay 16 bytes");

#endif
