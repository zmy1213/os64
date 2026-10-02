#include "memory/page_allocator.hpp"

namespace {

uint64_t align_up(uint64_t value) {
  return (value + kPageSize - 1) & ~(kPageSize - 1);
}

uint64_t clipped_end(const E820Entry& entry) {
  // 加法溢出也只能裁到管理上界，不能绕回低地址而错误释放保留页。
  if (entry.length > UINT64_MAX - entry.base) {
    return kAllocatorManagedLimit;
  }
  const uint64_t end = entry.base + entry.length;
  return end < kAllocatorManagedLimit ? end : kAllocatorManagedLimit;
}

void change_available_range(PageAllocator* allocator, uint64_t begin,
                             uint64_t end, bool available) {
  for (uint64_t page = begin / kPageSize; page < end / kPageSize; ++page) {
    const uint64_t bit = 1ULL << (page % 64);
    uint64_t& word = allocator->available_pages[page / 64];
    if (available) {
      word |= bit;
    } else {
      word &= ~bit;
    }
  }
}

bool usable(const E820Entry& entry) {
  return entry.type == kE820TypeUsable && (entry.extended_attributes & 1) != 0;
}

uint64_t allocate_between(PageAllocator* allocator, uint64_t minimum,
                           uint64_t limit) {
  if (allocator == nullptr || minimum >= kAllocatorManagedLimit ||
      minimum > UINT64_MAX - (kPageSize - 1)) {
    return 0;
  }
  if (minimum < kAllocatorMinAddress) {
    minimum = kAllocatorMinAddress;
  }
  minimum = align_up(minimum);
  if (limit > kAllocatorManagedLimit) {
    limit = kAllocatorManagedLimit;
  }
  limit &= ~(kPageSize - 1);
  if (minimum >= limit) {
    return 0;
  }

  const uint64_t first = minimum / kPageSize;
  const uint64_t last = limit / kPageSize;
  for (uint64_t word_index = first / 64;
       word_index <= (last - 1) / 64; ++word_index) {
    uint64_t candidates = allocator->available_pages[word_index] &
                          ~allocator->allocated_pages[word_index];
    if (word_index == first / 64) {
      candidates &= UINT64_MAX << (first % 64);
    }
    if (word_index == (last - 1) / 64 && (last % 64) != 0) {
      candidates &= (1ULL << (last % 64)) - 1;
    }
    if (candidates == 0) {
      continue;
    }
    const uint64_t bit_index = static_cast<uint64_t>(__builtin_ctzll(candidates));
    allocator->allocated_pages[word_index] |= 1ULL << bit_index;
    --allocator->free_page_count;
    return (word_index * 64 + bit_index) * kPageSize;
  }
  return 0;
}

}  // namespace

bool initialize_page_allocator(PageAllocator* allocator, const BootInfo* boot_info) {
  if (allocator == nullptr || boot_info == nullptr) {
    return false;
  }
  allocator->range_count = 0;
  allocator->active_range = 0;
  allocator->free_page_count = 0;
  for (uint64_t i = 0; i < kAllocatorBitmapWords; ++i) {
    allocator->available_pages[i] = 0;
    allocator->allocated_pages[i] = 0;
  }
  if (boot_info->memory_map_ptr == 0 || boot_info->memory_map_count == 0 ||
      boot_info->memory_map_count > kMaxUsableRanges ||
      boot_info->memory_map_entry_size != sizeof(E820Entry)) {
    return false;
  }
  const auto* entries = reinterpret_cast<const E820Entry*>(
      static_cast<uintptr_t>(boot_info->memory_map_ptr));

  // 第一遍加入可用整页；位图天然去重，同一页出现两次也不会被分配两次。
  for (uint16_t i = 0; i < boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];
    if (!usable(entry) || entry.length == 0 || entry.base >= kAllocatorManagedLimit) {
      continue;
    }
    uint64_t begin = entry.base < kAllocatorMinAddress ? kAllocatorMinAddress : entry.base;
    begin = align_up(begin);
    const uint64_t end = clipped_end(entry) & ~(kPageSize - 1);
    if (begin < end) {
      change_available_range(allocator, begin, end, true);
    }
  }
  // 第二遍排除保留区，哪怕保留区只覆盖了某一页的一部分，这整页也不再可用。
  for (uint16_t i = 0; i < boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];
    if (usable(entry) || entry.length == 0 || entry.base >= kAllocatorManagedLimit) {
      continue;
    }
    const uint64_t begin = entry.base & ~(kPageSize - 1);
    const uint64_t end = align_up(clipped_end(entry));
    change_available_range(allocator, begin, end, false);
  }
  // E820 只说明硬件 RAM 是否可用，不知道内核已经把其中一段拿来当启动栈。
  // 即使 BIOS 将这一段报告为 usable，也必须从我们的可分配页里排除。
  change_available_range(allocator, kAllocatorBootStackBase, kAllocatorBootStackLimit, false);
  // 目前启动卷在 0x80000，已经落在低 1 MiB 保留区；仍显式保护传入的位置，
  // 这样以后移动启动卷时，页分配器也不会误把它覆盖掉。
  if (boot_info->boot_volume_ptr < kAllocatorManagedLimit &&
      boot_info->boot_volume_sector_count != 0 && boot_info->boot_volume_sector_size != 0) {
    const uint64_t begin = boot_info->boot_volume_ptr & ~(kPageSize - 1);
    uint64_t end = boot_info->boot_volume_ptr +
                   static_cast<uint64_t>(boot_info->boot_volume_sector_count) *
                       boot_info->boot_volume_sector_size;
    if (end > kAllocatorManagedLimit) {
      end = kAllocatorManagedLimit;
    }
    change_available_range(allocator, begin, align_up(end), false);
  }

  // 日志用 ranges 只是可用区间的快照。分配和回收完全由位图控制。
  uint64_t range_begin = 0;
  for (uint64_t page = kAllocatorMinAddress / kPageSize; page <= kAllocatorPageCount; ++page) {
    const bool available = page < kAllocatorPageCount &&
        (allocator->available_pages[page / 64] & (1ULL << (page % 64))) != 0;
    if (available) {
      ++allocator->free_page_count;
      if (range_begin == 0) {
        range_begin = page * kPageSize;
      }
    } else if (range_begin != 0) {
      if (allocator->range_count < kMaxUsableRanges) {
        allocator->ranges[allocator->range_count++] = {range_begin, page * kPageSize};
      }
      range_begin = 0;
    }
  }
  return allocator->free_page_count != 0;
}

uint64_t alloc_page(PageAllocator* allocator) {
  return allocate_between(allocator, kAllocatorMinAddress, kAllocatorManagedLimit);
}

uint64_t alloc_page_below(PageAllocator* allocator, uint64_t limit) {
  return allocate_between(allocator, kAllocatorMinAddress, limit);
}

uint64_t alloc_page_at_least(PageAllocator* allocator, uint64_t minimum) {
  return allocate_between(allocator, minimum, kAllocatorManagedLimit);
}

bool page_allocator_owns_page(const PageAllocator* allocator, uint64_t physical_address) {
  if (allocator == nullptr || physical_address < kAllocatorMinAddress ||
      physical_address >= kAllocatorManagedLimit ||
      (physical_address & (kPageSize - 1)) != 0) {
    return false;
  }
  const uint64_t page = physical_address / kPageSize;
  const uint64_t bit = 1ULL << (page % 64);
  return (allocator->available_pages[page / 64] & bit) != 0 &&
         (allocator->allocated_pages[page / 64] & bit) != 0;
}

bool free_page(PageAllocator* allocator, uint64_t physical_address) {
  if (!page_allocator_owns_page(allocator, physical_address)) {
    return false;
  }
  const uint64_t page = physical_address / kPageSize;
  allocator->allocated_pages[page / 64] &= ~(1ULL << (page % 64));
  ++allocator->free_page_count;
  return true;
}

uint64_t count_free_pages(const PageAllocator* allocator) {
  return allocator != nullptr ? allocator->free_page_count : 0;
}
