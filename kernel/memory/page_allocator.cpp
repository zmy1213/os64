#include "memory/page_allocator.hpp"

namespace {

// 把地址向上对齐到 4 KiB 边界，比如 0x1003 会变成 0x2000。
uint64_t align_up(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  const uint64_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

// 把地址向下对齐到 4 KiB 边界，比如 0x2abc 会变成 0x2000。
uint64_t align_down(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  return value & ~(alignment - 1);
}

// 这一轮把 usable 类型粗暴理解成“只有 type=1 才能分配”。
bool is_usable_entry(const E820Entry& entry) {
  return entry.type == kE820TypeUsable && entry.length != 0;
}

}  // namespace

bool initialize_page_allocator(PageAllocator* allocator, const BootInfo* boot_info) {
  if (allocator == nullptr || boot_info == nullptr) {
    return false;
  }

  allocator->range_count = 0;
  allocator->active_range = 0;

  if (boot_info->memory_map_ptr == 0 ||
      boot_info->memory_map_entry_size != sizeof(E820Entry)) {
    return false;
  }

  const auto* entries =
      reinterpret_cast<const E820Entry*>(static_cast<uintptr_t>(boot_info->memory_map_ptr));

  // 把 E820 里的 usable 区域筛一遍，只留下真正适合“按页分配”的那部分。
  for (uint16_t i = 0; i < boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];
    if (!is_usable_entry(entry)) {
      continue;
    }

    uint64_t region_start = entry.base;
    uint64_t region_end = entry.base + entry.length;
    if (region_end <= region_start) {
      continue;
    }

    // 第一版故意只从 1 MiB 以上挑页，这样能避开 BIOS、bootloader、VGA 等低地址历史包袱。
    if (region_start < kAllocatorMinAddress) {
      region_start = kAllocatorMinAddress;
    }

    region_start = align_up(region_start, kPageSize);
    region_end = align_down(region_end, kPageSize);
    if (region_start >= region_end) {
      continue;
    }

    if (allocator->range_count >= kMaxUsableRanges) {
      break;
    }

    allocator->ranges[allocator->range_count].next_free = region_start;
    allocator->ranges[allocator->range_count].limit = region_end;
    ++allocator->range_count;
  }

  return allocator->range_count != 0;
}

uint64_t alloc_page(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  // 从当前 active_range 开始找，哪一段还有空页就从哪一段拿。
  for (uint16_t i = allocator->active_range; i < allocator->range_count; ++i) {
    PageAllocatorRange& range = allocator->ranges[i];
    if (range.next_free + kPageSize > range.limit) {
      allocator->active_range = static_cast<uint16_t>(i + 1);
      continue;
    }

    const uint64_t page = range.next_free;
    range.next_free += kPageSize;
    allocator->active_range = i;
    return page;
  }

  return 0;
}

uint64_t count_free_pages(const PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  uint64_t total = 0;
  for (uint16_t i = 0; i < allocator->range_count; ++i) {
    const PageAllocatorRange& range = allocator->ranges[i];
    if (range.limit <= range.next_free) {
      continue;
    }

    total += (range.limit - range.next_free) / kPageSize;
  }

  return total;
}
