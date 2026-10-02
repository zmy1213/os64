#include "memory/address_space.hpp"

#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint64_t kPageMask = 0x000FFFFFFFFFF000ULL;  // 页表项里真正保存物理页基址的部分。
constexpr uint64_t kPageLarge = 0x080;                 // PS=1：说明这一项已经是大页，不再指向下一层页表。
constexpr size_t kPageTableEntryCount = 512;           // x86_64 每层页表固定 512 项。

uint64_t* table_from_physical_address(uint64_t physical_address) {
  if (physical_address == 0 || physical_address >= kPagingBootIdentityLimit) {
    return nullptr;
  }

  return reinterpret_cast<uint64_t*>(
      static_cast<uintptr_t>(physical_address & kPageMask));
}

uint64_t allocate_clone_table_page(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return 0;
  }

  const uint64_t page = alloc_page_below(allocator, kPagingBootIdentityLimit);
  if (page == 0 || page >= kPagingBootIdentityLimit) {
    return 0;
  }

  auto* const table = table_from_physical_address(page);
  if (table == nullptr) {
    return 0;
  }

  memory_set(table, 0, kPagingPageSize);
  return page;
}

void destroy_table(PageAllocator* allocator, uint64_t physical_address,
                   uint8_t levels, bool release_user_pages) {
  auto* const table = table_from_physical_address(physical_address);
  if (table == nullptr || levels == 0) {
    return;
  }
  for (size_t index = 0; index < kPageTableEntryCount; ++index) {
    const uint64_t entry = table[index];
    if ((entry & kPagePresent) == 0) {
      continue;
    }
    if (levels > 1 && (entry & kPageLarge) == 0) {
      destroy_table(allocator, entry & kPageMask, levels - 1,
                    release_user_pages);
    } else if (levels == 1 && release_user_pages && (entry & kPageUser) != 0) {
      (void)free_page(allocator, entry & kPageMask);
    }
  }
  (void)free_page(allocator, physical_address);
}

// 递归克隆一层页表：
// - `source_physical_address` 指向原页表
// - `remaining_levels` 说明还剩几层要继续往下复制
//
// 它不会复制真正的普通数据页内容，
// 只是复制“描述映射关系的页表树”。
uint64_t clone_page_table_level(PageAllocator* allocator,
                                uint64_t source_physical_address,
                                uint8_t remaining_levels) {
  if (allocator == nullptr || source_physical_address == 0 ||
      remaining_levels == 0) {
    return 0;
  }

  auto* const source = table_from_physical_address(source_physical_address);
  if (source == nullptr) {
    return 0;
  }

  const uint64_t cloned_page = allocate_clone_table_page(allocator);
  if (cloned_page == 0) {
    return 0;
  }

  auto* const destination = table_from_physical_address(cloned_page);
  if (destination == nullptr) {
    return 0;
  }

  for (size_t index = 0; index < kPageTableEntryCount; ++index) {
    const uint64_t entry = source[index];
    if ((entry & kPagePresent) == 0) {
      destination[index] = 0;
      continue;
    }

    // 还没到最后一级、而且这项不是大页时，
    // 继续把“下一层页表页”也克隆出来。
    if (remaining_levels > 1 && (entry & kPageLarge) == 0) {
      const uint64_t child_physical_address = entry & kPageMask;
      const uint64_t cloned_child = clone_page_table_level(
          allocator, child_physical_address, remaining_levels - 1);
      if (cloned_child == 0) {
        destroy_table(allocator, cloned_page, remaining_levels, false);
        return 0;
      }

      destination[index] = (entry & ~kPageMask) | cloned_child;
      continue;
    }

    // A child receives kernel mappings, never aliases of its parent's user pages.
    destination[index] = (entry & kPageUser) != 0 ? 0 : entry;
  }

  return cloned_page;
}

void fill_common_layout(AddressSpace* space) {
  if (space == nullptr) {
    return;
  }

  // 这些字段描述的是“用户区的大轮廓”，
  // 不管是观察当前内核地址空间，还是新克隆一份用户地址空间，都共用同一套约定。
  space->user_region_base = kUserAddressSpaceBase;
  space->user_region_limit = kUserAddressSpaceLimit;
  space->default_user_stack_top = kUserAddressSpaceDefaultStackTop;
}

bool user_region_contains(uint64_t virtual_address) {
  return virtual_address >= kUserAddressSpaceBase &&
         virtual_address < kUserAddressSpaceLimit &&
         (virtual_address & (kPagingPageSize - 1)) == 0;
}

}  // namespace

bool initialize_kernel_address_space_view(AddressSpace* space) {
  if (space == nullptr) {
    return false;
  }

  memory_set(space, 0, sizeof(*space));
  fill_common_layout(space);

  space->root_physical_address = paging_current_root_physical();
  space->root_virtual_address =
      table_from_physical_address(space->root_physical_address);
  if (space->root_physical_address == 0 || space->root_virtual_address == nullptr) {
    return false;
  }

  // 这里只是“借用当前内核那份页表根来观察”，所以不拥有它。
  space->ready = true;
  space->owns_page_table_root = false;
  return true;
}

bool clone_current_address_space(AddressSpace* space, PageAllocator* allocator) {
  return clone_address_space_from_root(space, allocator,
                                       paging_current_root_physical());
}

bool clone_address_space_from_root(AddressSpace* space, PageAllocator* allocator,
                                   uint64_t source_root) {
  if (space == nullptr || allocator == nullptr) {
    return false;
  }

  memory_set(space, 0, sizeof(*space));
  fill_common_layout(space);

  const uint64_t cloned_root =
      clone_page_table_level(allocator, source_root, 4);
  if (cloned_root == 0) {
    return false;
  }

  // 从这里开始，`space` 就不再只是“看一看当前 CR3”，
  // 而是真的拥有一棵独立页表树，后面可以安全地往里面挂用户页。
  space->root_physical_address = cloned_root;
  space->root_virtual_address = table_from_physical_address(cloned_root);
  if (space->root_virtual_address == nullptr) {
    return false;
  }

  space->ready = true;
  space->owns_page_table_root = true;
  return true;
}

bool address_space_map_user_page(AddressSpace* space, PageAllocator* allocator,
                                 uint64_t virtual_address,
                                 uint64_t physical_address,
                                 uint64_t flags) {
  if (space == nullptr || allocator == nullptr || !space->ready ||
      !space->owns_page_table_root) {
    return false;
  }

  if (!user_region_contains(virtual_address) ||
      physical_address < kAllocatorMinAddress ||
      physical_address >= kPagingBootIdentityLimit ||
      (physical_address & (kPagingPageSize - 1)) != 0) {
    return false;
  }

  const bool already_mapped = address_space_user_range_valid(
      space, virtual_address, 1, false);
  if (already_mapped) {
    return false;
  }
  if (!map_page_in_root(allocator, space->root_physical_address,
                        virtual_address, physical_address,
                        flags | kPageUser)) {
    return false;
  }

  // 只有“原来没有映射，现在新挂上了”的情况才算新增用户页数量。
  if (!already_mapped) {
    ++space->mapped_user_pages;
  }

  return true;
}

uint64_t address_space_resolve_mapping(const AddressSpace* space,
                                       uint64_t virtual_address) {
  if (space == nullptr || !space->ready) {
    return 0;
  }

  return resolve_physical_address_in_root(space->root_physical_address,
                                          virtual_address);
}

bool address_space_destroy(AddressSpace* space, PageAllocator* allocator) {
  if (space == nullptr || allocator == nullptr || !space->ready ||
      !space->owns_page_table_root ||
      space->root_physical_address == paging_current_root_physical()) {
    return false;
  }
  destroy_table(allocator, space->root_physical_address, 4, true);
  memory_set(space, 0, sizeof(*space));
  return true;
}

bool address_space_user_range_valid(const AddressSpace* space,
                                     uint64_t address, size_t bytes,
                                     bool writable) {
  if (space == nullptr || !space->ready ||
      address < space->user_region_base || address >= space->user_region_limit ||
      bytes > space->user_region_limit - address) {
    return false;
  }
  if (bytes == 0) {
    return true;
  }
  const uint64_t required = kPagePresent | kPageUser |
                            (writable ? kPageWritable : 0);
  const uint64_t last_page = (address + bytes - 1) & ~(kPagingPageSize - 1);
  for (uint64_t page = address & ~(kPagingPageSize - 1);;
       page += kPagingPageSize) {
    auto* table = table_from_physical_address(space->root_physical_address);
    for (int shift = 39; shift >= 12; shift -= 9) {
      if (table == nullptr) {
        return false;
      }
      const uint64_t entry = table[(page >> shift) & 0x1FF];
      if ((entry & required) != required ||
          (shift > 12 && (entry & kPageLarge) != 0)) {
        return false;
      }
      if (shift > 12) {
        table = table_from_physical_address(entry & kPageMask);
      }
    }
    if (page == last_page) {
      return true;
    }
  }
}

bool address_space_copy_to_user(const AddressSpace* space, uint64_t address,
                                 const void* source, size_t bytes) {
  if ((source == nullptr && bytes != 0) ||
      !address_space_user_range_valid(space, address, bytes, true)) {
    return false;
  }
  const auto* cursor = static_cast<const uint8_t*>(source);
  while (bytes != 0) {
    const uint64_t physical = address_space_resolve_mapping(space, address);
    if (physical == 0 || physical >= kPagingBootIdentityLimit) {
      return false;
    }
    size_t chunk = kPagingPageSize - (address & (kPagingPageSize - 1));
    if (chunk > bytes) {
      chunk = bytes;
    }
    memory_copy(reinterpret_cast<void*>(static_cast<uintptr_t>(physical)),
                cursor, chunk);
    address += chunk;
    cursor += chunk;
    bytes -= chunk;
  }
  return true;
}
