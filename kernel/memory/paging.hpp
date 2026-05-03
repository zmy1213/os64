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

uint64_t paging_current_root_physical();

bool map_page(PageAllocator* allocator, uint64_t virtual_address,
              uint64_t physical_address, uint64_t flags);
bool map_page_in_root(PageAllocator* allocator, uint64_t root_physical_address,
                      uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags);
bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags);
uint64_t resolve_physical_address_in_root(uint64_t root_physical_address,
                                          uint64_t virtual_address);

#endif
