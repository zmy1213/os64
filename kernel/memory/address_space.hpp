#ifndef OS64_ADDRESS_SPACE_HPP
#define OS64_ADDRESS_SPACE_HPP

#include <stdbool.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

constexpr uint64_t kUserAddressSpaceBase = 0x0000000000400000ULL;       // 第一版先把“将来的用户区”放到 4 MiB 开始，避开当前内核身份映射的 0~2 MiB。
constexpr uint64_t kUserAddressSpaceLimit = 0x0000000000800000ULL;      // 先给出一个 4 MiB 大小的教学用用户区窗口。
constexpr uint64_t kUserAddressSpaceDefaultStackTop = kUserAddressSpaceLimit;  // 以后最小用户栈先从这个高地址往下长。

struct AddressSpace {
  bool ready;                               // 这份地址空间描述是否已经初始化完成。
  bool owns_page_table_root;                // `true` 表示这是独立克隆出来的页表根；`false` 表示只是共享观察当前内核那份 CR3。
  uint64_t root_physical_address;           // 这份地址空间真正对应的 PML4 物理地址。
  uint64_t* root_virtual_address;           // 由于早期只接受低地址页表页，所以现在还能直接拿一个恒等映射虚拟指针观察它。
  uint64_t user_region_base;                // 我们当前约定的“以后用户页优先往哪一段放”。
  uint64_t user_region_limit;               // 用户区上界（不包含这个地址本身）。
  uint64_t default_user_stack_top;          // 将来用户栈起始顶端先约定在这里。
  uint64_t mapped_user_pages;               // 这份地址空间里已经额外挂了多少张用户页，方便烟测和观察。
};

bool initialize_kernel_address_space_view(AddressSpace* space);
bool clone_current_address_space(AddressSpace* space, PageAllocator* allocator);
bool address_space_map_user_page(AddressSpace* space, PageAllocator* allocator,
                                 uint64_t virtual_address,
                                 uint64_t physical_address,
                                 uint64_t flags);
uint64_t address_space_resolve_mapping(const AddressSpace* space,
                                       uint64_t virtual_address);

#endif
