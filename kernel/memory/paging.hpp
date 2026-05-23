#ifndef OS64_PAGING_HPP
#define OS64_PAGING_HPP

#include <stdbool.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

constexpr uint64_t kPagingPageSize = 4096;             // x86_64 最基础的页大小就是 4 KiB。
constexpr uint64_t kPagingBootIdentityLimit = 0x200000;  // stage2 目前只保证低 2 MiB 被恒等映射。

constexpr uint64_t kPagePresent = 0x001;               // 页表项存在位。
constexpr uint64_t kPageWritable = 0x002;              // 页表项可写位。
constexpr uint64_t kPageUser = 0x004;                  // 以后用户态页会需要 U/S=1，这一轮先把这个标志位单独命名出来。

// 读取 CPU 当前正在使用的 CR3，也就是“当前活动页表根”的物理地址。
uint64_t paging_current_root_physical();

// 在“当前 CR3 对应的页表”里挂一张 4 KiB 页。
// `virtual_address` 是想让 CPU 以后访问的虚拟地址，
// `physical_address` 是真正映射到哪张物理页，
// `flags` 里通常会带可写/用户态这些权限位。
bool map_page(PageAllocator* allocator, uint64_t virtual_address,
              uint64_t physical_address, uint64_t flags);

// 和 `map_page()` 类似，但不一定改当前 CR3，
// 而是显式指定“往哪一份页表根里挂页”。
bool map_page_in_root(PageAllocator* allocator, uint64_t root_physical_address,
                      uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags);

// 批量做恒等映射：虚拟地址 == 物理地址。
// 这在早期内核特别常见，因为这样最容易调试，也最方便直接碰低地址页表页。
bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags);

// 反向查询：“这份页表里，这个虚拟地址最后会落到哪个物理地址？”
// 主要给调试、烟测和地址空间检查用。
uint64_t resolve_physical_address_in_root(uint64_t root_physical_address,
                                          uint64_t virtual_address);

#endif
