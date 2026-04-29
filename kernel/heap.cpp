#include "heap.hpp"

#include "paging.hpp"
#include "runtime.hpp"

namespace {

bool is_power_of_two(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

uint64_t align_up(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  const uint64_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

bool ensure_heap_mapping(KernelHeap* heap, uint64_t required_end) {
  if (heap == nullptr || heap->page_allocator == nullptr) {
    return false;
  }

  while (heap->mapped_limit < required_end) {
    const uint64_t physical_page = alloc_page(heap->page_allocator);
    if (physical_page == 0) {
      return false;
    }

    if (!map_page(heap->page_allocator, heap->mapped_limit, physical_page,
                  kPageWritable)) {
      return false;
    }

    // 新接进来的每一个页先清零，这样堆上读到的初值更可控。
    memory_set(reinterpret_cast<void*>(static_cast<uintptr_t>(heap->mapped_limit)),
               0, kPagingPageSize);
    heap->mapped_limit += kPagingPageSize;
  }

  return true;
}

}  // namespace

bool initialize_kernel_heap(KernelHeap* heap, PageAllocator* allocator) {
  if (heap == nullptr || allocator == nullptr) {
    return false;
  }

  heap->page_allocator = allocator;
  heap->virtual_start = kKernelHeapStart;
  heap->next_free = kKernelHeapStart;
  heap->mapped_limit = kKernelHeapStart;
  heap->virtual_limit = kKernelHeapLimit;
  return heap->virtual_start < heap->virtual_limit;
}

void* heap_alloc(KernelHeap* heap, size_t size, size_t alignment) {
  if (heap == nullptr || heap->page_allocator == nullptr || size == 0) {
    return nullptr;
  }

  if (alignment == 0) {
    alignment = 1;
  }

  if (!is_power_of_two(alignment)) {
    return nullptr;
  }

  const uint64_t aligned_start =
      align_up(heap->next_free, static_cast<uint64_t>(alignment));
  const uint64_t allocation_end = aligned_start + static_cast<uint64_t>(size);

  if (allocation_end < aligned_start || allocation_end > heap->virtual_limit) {
    return nullptr;
  }

  if (!ensure_heap_mapping(heap, align_up(allocation_end, kPagingPageSize))) {
    return nullptr;
  }

  heap->next_free = allocation_end;
  auto* allocation =
      reinterpret_cast<void*>(static_cast<uintptr_t>(aligned_start));
  memory_set(allocation, 0, size);    // 这一版先保证每次拿到的新内存都是清零的。
  return allocation;
}

uint64_t heap_used_bytes(const KernelHeap* heap) {
  if (heap == nullptr || heap->next_free < heap->virtual_start) {
    return 0;
  }

  return heap->next_free - heap->virtual_start;
}

uint64_t heap_mapped_bytes(const KernelHeap* heap) {
  if (heap == nullptr || heap->mapped_limit < heap->virtual_start) {
    return 0;
  }

  return heap->mapped_limit - heap->virtual_start;
}
