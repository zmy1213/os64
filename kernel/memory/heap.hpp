#ifndef OS64_HEAP_HPP
#define OS64_HEAP_HPP

#include <stddef.h>
#include <stdint.h>

#include "memory/page_allocator.hpp"

constexpr uint64_t kKernelHeapStart = 0x0000000000400000ULL;  // 第一版把堆虚拟地址放在 4 MiB。
constexpr uint64_t kKernelHeapLimit = 0x0000000000800000ULL;  // 先给它预留到 8 MiB，便于调试。

struct KernelHeapFreeRegion;

// 这是第二版内核堆的状态。
// 和上一轮不同，这一版不再只是“next_free 一路往前冲”，
// 而是开始维护一条真正的空闲区链表。
struct KernelHeap {
  PageAllocator* page_allocator;    // 真正给堆找物理页的人，还是底层页分配器。
  uint64_t virtual_start;           // 堆从哪个虚拟地址开始。
  uint64_t mapped_limit;            // 已经映射好的堆虚拟地址上界（不包含自身）。
  uint64_t virtual_limit;           // 这版堆允许长到哪儿为止，先固定一个上界。
  uint64_t used_bytes;              // 当前真正还在使用中的 payload 总字节数。
  uint64_t active_allocations;      // 现在还有多少块分配尚未 free。
  uint64_t total_allocations;       // 自堆初始化以来，一共成功分配过多少次。
  uint64_t failed_allocations;      // 自堆初始化以来，一共有多少次分配失败。
  KernelHeapFreeRegion* free_list;  // 所有空闲区按地址顺序串成一条链。
};

// 建好一份“还没真正映射任何页”的空堆。
// 这里只是把状态清干净，并记住以后要向哪个页分配器要物理页。
bool initialize_kernel_heap(KernelHeap* heap, PageAllocator* allocator);

// 分配一块堆内存。
// `size` 是调用者真正想要的 payload 大小；
// `alignment` 是这块内存起始地址至少要满足的对齐要求。
void* heap_alloc(KernelHeap* heap, size_t size, size_t alignment);

// 释放之前由 `heap_alloc()` 返回的块。
// 成功返回 `true`，失败通常意味着传进来的指针不是合法堆块。
bool heap_free(KernelHeap* heap, void* allocation);

// 下面这些都是“观察堆当前状态”的接口，主要给调试日志和 smoke test 用。
uint64_t heap_used_bytes(const KernelHeap* heap);
uint64_t heap_mapped_bytes(const KernelHeap* heap);
uint64_t heap_free_bytes(const KernelHeap* heap);
uint64_t heap_active_allocations(const KernelHeap* heap);
uint64_t heap_total_allocations(const KernelHeap* heap);
uint64_t heap_failed_allocations(const KernelHeap* heap);

#endif
