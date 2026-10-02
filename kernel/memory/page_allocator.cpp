#include "memory/page_allocator.hpp"

namespace {

constexpr uint64_t kRecyclableLimit = 0x200000;

void mark_allocated(PageAllocator* allocator, uint64_t page) {
  if (page >= kAllocatorMinAddress && page < kRecyclableLimit) {
    const uint64_t index = (page - kAllocatorMinAddress) / kPageSize;
    allocator->low_page_allocated[index / 64] |= 1ULL << (index % 64);
  }
}

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
  allocator->recycled_page_head = 0;
  allocator->recycled_page_count = 0;
  for (uint64_t& word : allocator->low_page_allocated) {
    word = 0;
  }

  if (boot_info->memory_map_ptr == 0 ||
      boot_info->memory_map_entry_size != sizeof(E820Entry)) {
    return false;
  }

  const auto* entries =
      reinterpret_cast<const E820Entry*>(static_cast<uintptr_t>(boot_info->memory_map_ptr));

  // 把 E820 里的 usable 区域筛一遍，只留下真正适合“按页分配”的那部分。
  // 这里做完以后，`allocator->ranges[]` 就变成内核后续真正会消费的“可用页池”。
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
  return alloc_page_below(allocator, UINT64_MAX);
}

uint64_t alloc_page_below(PageAllocator* allocator, uint64_t limit) {
  if (allocator == nullptr) {
    return 0;
  }

  uint64_t* link = &allocator->recycled_page_head;
  while (*link != 0) {
    const uint64_t page = *link;
    auto* const next = reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(page));
    if (page < limit && limit - page >= kPageSize) {
      *link = *next;
      --allocator->recycled_page_count;
      mark_allocated(allocator, page);
      return page;
    }
    link = next;
  }

  // 从当前 active_range 开始找，哪一段还有空页就从哪一段拿。
  // 这就是第一版最朴素的“顺序分配”策略：不回收、不合并，只是一直往前切页。
  for (uint16_t i = 0; i < allocator->range_count; ++i) {
    PageAllocatorRange& range = allocator->ranges[i];
    if (range.next_free >= range.limit ||
        range.limit - range.next_free < kPageSize ||
        range.next_free >= limit || limit - range.next_free < kPageSize) {
      continue;
    }

    const uint64_t page = range.next_free;
    range.next_free += kPageSize;
    allocator->active_range = i;
    mark_allocated(allocator, page);
    return page;
  }

  return 0;
}

bool free_page(PageAllocator* allocator, uint64_t physical_address) {
  if (allocator == nullptr || physical_address < kAllocatorMinAddress ||
      physical_address >= kRecyclableLimit ||
      (physical_address & (kPageSize - 1)) != 0) {
    return false;
  }
  const uint64_t index = (physical_address - kAllocatorMinAddress) / kPageSize;
  const uint64_t bit = 1ULL << (index % 64);
  uint64_t& word = allocator->low_page_allocated[index / 64];
  if ((word & bit) == 0) {
    return false;
  }
  word &= ~bit;
  *reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(physical_address)) =
      allocator->recycled_page_head;
  allocator->recycled_page_head = physical_address;
  ++allocator->recycled_page_count;
  return true;
}

uint64_t alloc_page_at_least(PageAllocator* allocator, uint64_t minimum) {
  if (allocator == nullptr || minimum > UINT64_MAX - (kPageSize - 1)) {
    return 0;
  }
  minimum = align_up(minimum, kPageSize);
  for (uint16_t i = 0; i < allocator->range_count; ++i) {
    PageAllocatorRange& range = allocator->ranges[i];
    if (range.next_free >= range.limit || range.limit - range.next_free < kPageSize ||
        minimum >= range.limit || range.limit - minimum < kPageSize) {
      continue;
    }
    if (range.next_free < minimum) {
      if (allocator->range_count == kMaxUsableRanges) {
        continue;
      }
      // Preserve the low-address prefix for page tables and user physical pages.
      PageAllocatorRange& suffix = allocator->ranges[allocator->range_count++];
      suffix.next_free = minimum + kPageSize;
      suffix.limit = range.limit;
      range.limit = minimum;
      mark_allocated(allocator, minimum);
      return minimum;
    }
    const uint64_t page = range.next_free;
    range.next_free += kPageSize;
    mark_allocated(allocator, page);
    return page;
  }
  return 0;
}

uint64_t count_free_pages(const PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  // 每一段还能拿多少页 = (limit - next_free) / 4096，
  // 最后把所有 usable 段的剩余页数加起来。
  uint64_t total = allocator->recycled_page_count;
  for (uint16_t i = 0; i < allocator->range_count; ++i) {
    const PageAllocatorRange& range = allocator->ranges[i];
    if (range.limit <= range.next_free) {
      continue;
    }

    total += (range.limit - range.next_free) / kPageSize;
  }

  return total;
}
