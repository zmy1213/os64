#include "paging.hpp"

#include "runtime.hpp"

namespace {

constexpr uint64_t kPageMask = 0x000FFFFFFFFFF000ULL;  // 页表项里真正的物理地址部分。
constexpr uint64_t kPageLarge = 0x080;                 // PDE 里 PS=1 表示 2 MiB 大页。

uint64_t read_cr3() {
  uint64_t value = 0;
  asm volatile("mov %%cr3, %0" : "=r"(value));
  return value;
}

void invalidate_page(uint64_t address) {
  asm volatile("invlpg (%0)" : : "r"(address) : "memory");
}

bool is_page_aligned(uint64_t address) {
  return (address & (kPagingPageSize - 1)) == 0;
}

uint16_t pml4_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 39) & 0x1FF);
}

uint16_t pdpt_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 30) & 0x1FF);
}

uint16_t pd_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 21) & 0x1FF);
}

uint16_t pt_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 12) & 0x1FF);
}

uint64_t* table_from_entry(uint64_t entry) {
  return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(entry & kPageMask));
}

uint64_t allocate_page_table_page(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  const uint64_t page = alloc_page(allocator);
  if (page == 0) {
    return 0;
  }

  // 这一版页表管理器还很早期，假设新页表页本身也必须能通过当前恒等映射直接访问。
  // 所以我们暂时只接受落在低 2 MiB 里的页表页。后面做更完整映射后再放开这个限制。
  if (page >= kPagingBootIdentityLimit) {
    return 0;
  }

  memory_set(reinterpret_cast<void*>(static_cast<uintptr_t>(page)), 0,
             kPagingPageSize);
  return page;
}

uint64_t* ensure_next_level(PageAllocator* allocator, uint64_t* table,
                            uint16_t index) {
  if (table == nullptr) {
    return nullptr;
  }

  const uint64_t entry = table[index];
  if ((entry & kPagePresent) != 0) {
    if ((entry & kPageLarge) != 0) {
      return nullptr;  // 这一层如果已经是大页，就不能再往下挂普通页表。
    }

    return table_from_entry(entry);
  }

  const uint64_t new_page = allocate_page_table_page(allocator);
  if (new_page == 0) {
    return nullptr;
  }

  table[index] = new_page | kPagePresent | kPageWritable;
  return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(new_page));
}

}  // namespace

bool map_page(PageAllocator* allocator, uint64_t virtual_address,
              uint64_t physical_address, uint64_t flags) {
  if (allocator == nullptr) {
    return false;
  }

  if (!is_page_aligned(virtual_address) || !is_page_aligned(physical_address)) {
    return false;
  }

  auto* pml4 =
      reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(read_cr3() & kPageMask));
  auto* pdpt = ensure_next_level(allocator, pml4, pml4_index(virtual_address));
  if (pdpt == nullptr) {
    return false;
  }

  auto* pd = ensure_next_level(allocator, pdpt, pdpt_index(virtual_address));
  if (pd == nullptr) {
    return false;
  }

  auto* pt = ensure_next_level(allocator, pd, pd_index(virtual_address));
  if (pt == nullptr) {
    return false;
  }

  const uint16_t index = pt_index(virtual_address);
  pt[index] = (physical_address & kPageMask) | flags | kPagePresent;
  invalidate_page(virtual_address);
  return true;
}

bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags) {
  if (start > end || !is_page_aligned(start) || !is_page_aligned(end)) {
    return false;
  }

  for (uint64_t address = start; address < end; address += kPagingPageSize) {
    if (!map_page(allocator, address, address, flags)) {
      return false;
    }
  }

  return true;
}
