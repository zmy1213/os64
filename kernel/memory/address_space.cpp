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

  const uint64_t page = alloc_page(allocator);
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

    if (remaining_levels > 1 && (entry & kPageLarge) == 0) {
      const uint64_t child_physical_address = entry & kPageMask;
      const uint64_t cloned_child = clone_page_table_level(
          allocator, child_physical_address, remaining_levels - 1);
      if (cloned_child == 0) {
        return 0;
      }

      destination[index] = (entry & ~kPageMask) | cloned_child;
      continue;
    }

    destination[index] = entry;
  }

  return cloned_page;
}

void fill_common_layout(AddressSpace* space) {
  if (space == nullptr) {
    return;
  }

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

  space->ready = true;
  space->owns_page_table_root = false;
  return true;
}

bool clone_current_address_space(AddressSpace* space, PageAllocator* allocator) {
  if (space == nullptr || allocator == nullptr) {
    return false;
  }

  memory_set(space, 0, sizeof(*space));
  fill_common_layout(space);

  const uint64_t current_root = paging_current_root_physical();
  const uint64_t cloned_root =
      clone_page_table_level(allocator, current_root, 4);
  if (cloned_root == 0) {
    return false;
  }

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
      (physical_address & (kPagingPageSize - 1)) != 0) {
    return false;
  }

  const bool already_mapped =
      address_space_resolve_mapping(space, virtual_address) != 0;
  if (!map_page_in_root(allocator, space->root_physical_address,
                        virtual_address, physical_address,
                        flags | kPageUser)) {
    return false;
  }

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
