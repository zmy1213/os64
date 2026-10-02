#include "memory/paging.hpp"

#include "runtime/runtime.hpp"

namespace {

constexpr uint64_t kPageMask = 0x000FFFFFFFFFF000ULL;  // 页表项里真正的物理地址部分。
constexpr uint64_t kPageLarge = 0x080;                 // PDE 里 PS=1 表示 2 MiB 大页。
constexpr uint64_t kHugePageMask = 0x000FFFFFC0000000ULL;   // 1 GiB 大页的物理页基址部分。
constexpr uint64_t kLargePageMask = 0x000FFFFFFFE00000ULL;  // 2 MiB 大页的物理页基址部分。
constexpr uint64_t kHugePageSize = 1ULL << 30;
constexpr uint64_t kLargePageSize = 1ULL << 21;

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

// 下面这几个 helper 都是在做同一件事：
// 从一个 48 位 canonical 虚拟地址里，拆出四级页表各自使用的 9 位索引。
uint16_t pdpt_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 30) & 0x1FF);
}

uint16_t pd_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 21) & 0x1FF);
}

uint16_t pt_index(uint64_t address) {
  return static_cast<uint16_t>((address >> 12) & 0x1FF);
}

uint64_t* table_from_physical_address(uint64_t physical_address) {
  return reinterpret_cast<uint64_t*>(
      static_cast<uintptr_t>(physical_address & kPageMask));
}

// 页表项里同时混着“物理地址 + 标志位”，
// 所以先掐掉低位 flags，才能重新把它当作下一层页表地址来访问。
uint64_t* table_from_entry(uint64_t entry) {
  return table_from_physical_address(entry);
}

uint64_t allocate_page_table_page(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  const uint64_t page = alloc_page_below(allocator, kPagingBootIdentityLimit);
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
                            uint16_t index,
                            uint64_t required_flags) {
  if (table == nullptr) {
    return nullptr;
  }

  // 如果这一层已经有下级页表，就直接复用；
  // 但要顺手把“父级至少应该具备的权限位”补齐，不然下面的用户页可能实际走不过去。
  uint64_t entry = table[index];
  if ((entry & kPagePresent) != 0) {
    if ((entry & kPageLarge) != 0) {
      return nullptr;  // 这一层如果已经是大页，就不能再往下挂普通页表。
    }

    const uint64_t propagated_flags =
        required_flags & (kPageWritable | kPageUser);
    if ((entry & propagated_flags) != propagated_flags) {
      entry |= propagated_flags;
      table[index] = entry;
    }

    return table_from_entry(entry);
  }

  const uint64_t new_page = allocate_page_table_page(allocator);
  if (new_page == 0) {
    return nullptr;
  }

  table[index] =
      new_page | kPagePresent | kPageWritable |
      (required_flags & kPageUser);
  return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(new_page));
}

}  // namespace

uint64_t paging_current_root_physical() {
  return read_cr3() & kPageMask;
}

bool map_page(PageAllocator* allocator, uint64_t virtual_address,
              uint64_t physical_address, uint64_t flags) {
  return map_page_in_root(allocator, paging_current_root_physical(),
                          virtual_address, physical_address, flags);
}

bool map_page_in_root(PageAllocator* allocator, uint64_t root_physical_address,
                      uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags) {
  if (allocator == nullptr || root_physical_address == 0) {
    return false;
  }

  if (!is_page_aligned(virtual_address) || !is_page_aligned(physical_address)) {
    return false;
  }

  auto* pml4 = table_from_physical_address(root_physical_address);
  auto* pdpt = ensure_next_level(allocator, pml4, pml4_index(virtual_address),
                                 flags);
  if (pdpt == nullptr) {
    return false;
  }

  auto* pd = ensure_next_level(allocator, pdpt, pdpt_index(virtual_address),
                               flags);
  if (pd == nullptr) {
    return false;
  }

  auto* pt = ensure_next_level(allocator, pd, pd_index(virtual_address),
                               flags);
  if (pt == nullptr) {
    return false;
  }

  // 真的“把最后一格 PTE 填进去”的时刻就在这里。
  const uint16_t index = pt_index(virtual_address);
  pt[index] = (physical_address & kPageMask) | flags | kPagePresent;

  // 如果改的是当前正在运行的页表，TLB 里旧缓存可能还在，
  // 所以要用 `invlpg` 把这一个虚拟页的旧翻译作废。
  if (root_physical_address == paging_current_root_physical()) {
    invalidate_page(virtual_address);
  }

  return true;
}

bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags) {
  if (start > end || !is_page_aligned(start) || !is_page_aligned(end)) {
    return false;
  }

  // 一页一页循环挂上“虚拟地址 == 物理地址”的映射。
  for (uint64_t address = start; address < end; address += kPagingPageSize) {
    if (!map_page(allocator, address, address, flags)) {
      return false;
    }
  }

  return true;
}

uint64_t resolve_physical_address_in_root(uint64_t root_physical_address,
                                          uint64_t virtual_address) {
  if (root_physical_address == 0) {
    return 0;
  }

  // 这整个函数就是在“手工走页表”：
  // PML4 -> PDPT -> PD -> PT，途中任何一级没 present 都说明映射不存在。
  auto* const pml4 = table_from_physical_address(root_physical_address);
  const uint64_t pml4e = pml4[pml4_index(virtual_address)];
  if ((pml4e & kPagePresent) == 0) {
    return 0;
  }

  auto* const pdpt = table_from_entry(pml4e);
  const uint64_t pdpte = pdpt[pdpt_index(virtual_address)];
  if ((pdpte & kPagePresent) == 0) {
    return 0;
  }

  if ((pdpte & kPageLarge) != 0) {
    return (pdpte & kHugePageMask) + (virtual_address & (kHugePageSize - 1));
  }

  auto* const pd = table_from_entry(pdpte);
  const uint64_t pde = pd[pd_index(virtual_address)];
  if ((pde & kPagePresent) == 0) {
    return 0;
  }

  if ((pde & kPageLarge) != 0) {
    return (pde & kLargePageMask) + (virtual_address & (kLargePageSize - 1));
  }

  auto* const pt = table_from_entry(pde);
  const uint64_t pte = pt[pt_index(virtual_address)];
  if ((pte & kPagePresent) == 0) {
    return 0;
  }

  return (pte & kPageMask) + (virtual_address & (kPagingPageSize - 1));
}
