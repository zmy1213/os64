#ifndef OS64_HEAP_HPP
#define OS64_HEAP_HPP

#include <stddef.h>
#include <stdint.h>

#include "page_allocator.hpp"

constexpr uint64_t kKernelHeapStart = 0x0000000000400000ULL;  // 第一版把堆虚拟地址放在 4 MiB。
constexpr uint64_t kKernelHeapLimit = 0x0000000000800000ULL;  // 先给它预留到 8 MiB，便于调试。

// 这就是第一版内核堆的全部状态。
// 它非常简单：只会“往前长”，不会 free。
struct KernelHeap {
  PageAllocator* page_allocator;    // 真正给堆找物理页的人，还是底层页分配器。
  uint64_t virtual_start;           // 堆从哪个虚拟地址开始。
  uint64_t next_free;               // 下一次分配应该从哪里开始切。
  uint64_t mapped_limit;            // 已经映射好的堆虚拟地址上界（不包含自身）。
  uint64_t virtual_limit;           // 这版堆允许长到哪儿为止，先固定一个上界。
};

bool initialize_kernel_heap(KernelHeap* heap, PageAllocator* allocator);
void* heap_alloc(KernelHeap* heap, size_t size, size_t alignment);
uint64_t heap_used_bytes(const KernelHeap* heap);
uint64_t heap_mapped_bytes(const KernelHeap* heap);

#endif
