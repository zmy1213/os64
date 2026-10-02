#ifndef OS64_PAGE_ALLOCATOR_HPP
#define OS64_PAGE_ALLOCATOR_HPP

#include <stdint.h>

#include "boot/boot_info.hpp"

constexpr uint64_t kPageSize = 4096;              // 先固定 4 KiB 页，这是 x86_64 最基础的页大小。
constexpr uint64_t kAllocatorMinAddress = 0x100000;  // 第一版先只从 1 MiB 以上挑页，避开低地址杂项区。
constexpr uint16_t kMaxUsableRanges = 16;         // 和 stage2 当前最多缓存 16 条 E820 记录保持一致。

// 每个 range 代表“一段已经确认可用，并且已经按页对齐好的物理内存区间”。
struct PageAllocatorRange {
  uint64_t next_free;   // 下一次 alloc_page() 应该从哪里拿页。
  uint64_t limit;       // 这个区间的结束地址（不包含 limit 本身）。
};

// 这是第一版物理页分配器的全部状态。
// 现在先不做 buddy/slab，只做“从 usable 区域里按顺序切 4 KiB 页”。
struct PageAllocator {
  PageAllocatorRange ranges[kMaxUsableRanges];  // 记录所有能拿来分配页的 usable 区间。
  uint16_t range_count;                         // 一共收集到了多少段 usable 区间。
  uint16_t active_range;                        // 当前正在从哪一段里分配。
  uint64_t recycled_page_head;
  uint64_t recycled_page_count;
  // Only the early identity-mapped 1–2 MiB pool can currently be freed.
  // Ownership bits reject double frees without touching live page contents.
  uint64_t low_page_allocated[4];
};

// 读 BootInfo 里的 E820 表，把“可用物理内存”整理成若干可分配区间。
// `allocator` 是要被填好的分配器状态对象；
// `boot_info` 提供 BIOS -> stage2 -> kernel 传下来的内存地图。
bool initialize_page_allocator(PageAllocator* allocator, const BootInfo* boot_info);

// 真正分配 1 个 4 KiB 物理页。
// 成功返回物理地址；失败返回 0。
uint64_t alloc_page(PageAllocator* allocator);
uint64_t alloc_page_below(PageAllocator* allocator, uint64_t limit);
uint64_t alloc_page_at_least(PageAllocator* allocator, uint64_t minimum);
bool free_page(PageAllocator* allocator, uint64_t physical_address);

// 统计“目前还剩下多少张没被拿走的页”，主要给调试和状态输出用。
uint64_t count_free_pages(const PageAllocator* allocator);

#endif
