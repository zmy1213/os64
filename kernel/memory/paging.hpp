#ifndef OS64_PAGING_HPP
#define OS64_PAGING_HPP

#include <stdbool.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

constexpr uint64_t kPagingPageSize = 4096;             // x86_64 最基础的页大小就是 4 KiB。
constexpr uint64_t kPagingBootIdentityLimit = 0x200000;  // stage2 目前只保证低 2 MiB 被恒等映射。
constexpr uint64_t kPagingDirectMapBase = 0xFFFF800000000000ULL;

constexpr uint64_t kPagePresent = 0x001;               // 页表项存在位。
constexpr uint64_t kPageWritable = 0x002;              // 页表项可写位。
constexpr uint64_t kPageUser = 0x004;                  // U/S=1 允许 ring 3 访问；整条页表路径都必须允许用户访问。
constexpr uint64_t kPageNoExecute = 1ULL << 63;

// 用 supervisor-only 的高虚拟地址访问物理 RAM：虚拟地址=基址+物理地址。
// 初始化时只用旧低地址恒等映射创建两张页表；完成后页表可以放在整个管理范围内。
bool paging_initialize_direct_map(PageAllocator* allocator);
void* paging_physical_pointer(uint64_t physical_address);
uint64_t paging_managed_physical_limit();
bool paging_no_execute_enabled();

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
// Map one device register page below 4 GiB, supervisor-only and uncached.
// Install before AP startup/user roots; this never treats device memory as RAM.
bool map_device_page(PageAllocator* allocator, uint64_t virtual_address,
                     uint64_t physical_address);

// 批量做恒等映射：虚拟地址 == 物理地址。
// 用于保留启动阶段的低地址映射；一般物理内存访问改用上面的 direct map。
bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags);

// 反向查询：“这份页表里，这个虚拟地址最后会落到哪个物理地址？”
// 主要给调试、烟测和地址空间检查用。
uint64_t resolve_physical_address_in_root(uint64_t root_physical_address,
                                          uint64_t virtual_address);

#endif
