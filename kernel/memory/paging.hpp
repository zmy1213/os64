#ifndef OS64_PAGING_HPP
#define OS64_PAGING_HPP

#include <stdbool.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

constexpr uint64_t kPagingPageSize = 4096;             // x86_64 最基础的页大小就是 4 KiB。
constexpr uint64_t kPagingBootIdentityLimit = 0x200000;  // stage2 目前只保证低 2 MiB 被恒等映射。

constexpr uint64_t kPagePresent = 0x001;               // 页表项存在位。
constexpr uint64_t kPageWritable = 0x002;              // 页表项可写位。

bool map_page(PageAllocator* allocator, uint64_t virtual_address,
              uint64_t physical_address, uint64_t flags);
bool map_identity_range(PageAllocator* allocator, uint64_t start, uint64_t end,
                        uint64_t flags);

#endif
