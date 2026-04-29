#include "memory/heap.hpp"

#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

struct alignas(16) KernelHeapFreeRegion {
  uint64_t region_bytes;             // 这段空闲区总共有多少字节，包含这个头本身。
  KernelHeapFreeRegion* next;        // 下一段空闲区。
};

struct alignas(16) KernelHeapAllocationHeader {
  uint64_t region_start;             // 这次分配实际占掉的整段区域从哪里开始。
  uint64_t region_bytes;             // 这次分配实际占掉了多少字节。
  uint64_t payload_bytes;            // 交给调用者的有效 payload 大小。
  uint64_t magic;                    // 用一个魔数粗暴判断 free 的指针是不是像样。
};

namespace {

constexpr uint64_t kHeapAlignment = 16;   // 先把堆元数据和普通小对象都按 16 字节对齐。
constexpr uint64_t kAllocationMagic = 0x48454150414C4C4FULL;  // ASCII 看起来像 "HEAPALLO"。

void record_failed_allocation(KernelHeap* heap) {
  if (heap != nullptr) {
    ++heap->failed_allocations;
  }
}

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

uint64_t address_of(const void* pointer) {
  return reinterpret_cast<uint64_t>(pointer);
}

uint64_t free_region_start(const KernelHeapFreeRegion* region) {
  return address_of(region);
}

uint64_t free_region_end(const KernelHeapFreeRegion* region) {
  return free_region_start(region) + region->region_bytes;
}

uint64_t minimum_growth_bytes(uint64_t payload_bytes, uint64_t alignment) {
  return sizeof(KernelHeapAllocationHeader) + payload_bytes + alignment +
         sizeof(KernelHeapFreeRegion);
}

void merge_forward(KernelHeapFreeRegion* region) {
  while (region != nullptr && region->next != nullptr &&
         free_region_end(region) == free_region_start(region->next)) {
    region->region_bytes += region->next->region_bytes;
    region->next = region->next->next;
  }
}

void insert_free_region(KernelHeap* heap, uint64_t region_start,
                        uint64_t region_bytes) {
  if (heap == nullptr || region_bytes < sizeof(KernelHeapFreeRegion)) {
    return;
  }

  auto* const inserted_region = reinterpret_cast<KernelHeapFreeRegion*>(
      static_cast<uintptr_t>(region_start));
  inserted_region->region_bytes = region_bytes;
  inserted_region->next = nullptr;

  KernelHeapFreeRegion* previous = nullptr;
  KernelHeapFreeRegion* current = heap->free_list;

  while (current != nullptr && free_region_start(current) < region_start) {
    previous = current;
    current = current->next;
  }

  inserted_region->next = current;
  if (previous == nullptr) {
    heap->free_list = inserted_region;
  } else {
    previous->next = inserted_region;
  }

  KernelHeapFreeRegion* merged_region = inserted_region;
  if (previous != nullptr && free_region_end(previous) == region_start) {
    previous->region_bytes += inserted_region->region_bytes;
    previous->next = inserted_region->next;
    merged_region = previous;
  }

  merge_forward(merged_region);
}

bool ensure_heap_mapping(KernelHeap* heap, uint64_t additional_bytes) {
  if (heap == nullptr || heap->page_allocator == nullptr) {
    return false;
  }

  if (additional_bytes == 0) {
    return true;
  }

  const uint64_t previous_limit = heap->mapped_limit;
  const uint64_t required_end = previous_limit + additional_bytes;
  if (required_end < previous_limit || required_end > heap->virtual_limit) {
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

  insert_free_region(heap, previous_limit, heap->mapped_limit - previous_limit);
  return true;
}

bool try_allocate_from_region(KernelHeap* heap, KernelHeapFreeRegion* region,
                              KernelHeapFreeRegion* previous,
                              uint64_t payload_bytes, uint64_t alignment,
                              void** allocation) {
  if (heap == nullptr || region == nullptr || allocation == nullptr) {
    return false;
  }

  const uint64_t region_start = free_region_start(region);
  const uint64_t region_end = free_region_end(region);

  uint64_t payload_start = align_up(region_start + sizeof(KernelHeapAllocationHeader),
                                    alignment);
  uint64_t header_start = payload_start - sizeof(KernelHeapAllocationHeader);
  uint64_t prefix_bytes = header_start - region_start;

  while (prefix_bytes != 0 && prefix_bytes < sizeof(KernelHeapFreeRegion)) {
    payload_start = align_up(payload_start + sizeof(KernelHeapFreeRegion),
                             alignment);
    header_start = payload_start - sizeof(KernelHeapAllocationHeader);
    prefix_bytes = header_start - region_start;
  }

  const uint64_t payload_end = payload_start + payload_bytes;
  if (payload_end < payload_start || payload_end > region_end) {
    return false;
  }

  uint64_t allocation_end = payload_end;
  uint64_t suffix_bytes = region_end - allocation_end;
  if (suffix_bytes != 0 && suffix_bytes < sizeof(KernelHeapFreeRegion)) {
    allocation_end = region_end;  // 太小的尾巴不值得单独保留，直接并进本次分配。
    suffix_bytes = 0;
  }

  if (previous == nullptr) {
    heap->free_list = region->next;
  } else {
    previous->next = region->next;
  }

  if (prefix_bytes >= sizeof(KernelHeapFreeRegion)) {
    insert_free_region(heap, region_start, prefix_bytes);
  }
  if (suffix_bytes >= sizeof(KernelHeapFreeRegion)) {
    insert_free_region(heap, allocation_end, suffix_bytes);
  }

  auto* const header = reinterpret_cast<KernelHeapAllocationHeader*>(
      static_cast<uintptr_t>(header_start));
  header->region_start = header_start;
  header->region_bytes = allocation_end - header_start;
  header->payload_bytes = payload_bytes;
  header->magic = kAllocationMagic;

  *allocation = reinterpret_cast<void*>(static_cast<uintptr_t>(payload_start));
  heap->used_bytes += payload_bytes;
  return true;
}

}  // namespace

bool initialize_kernel_heap(KernelHeap* heap, PageAllocator* allocator) {
  if (heap == nullptr || allocator == nullptr) {
    return false;
  }

  heap->page_allocator = allocator;
  heap->virtual_start = kKernelHeapStart;
  heap->mapped_limit = kKernelHeapStart;
  heap->virtual_limit = kKernelHeapLimit;
  heap->used_bytes = 0;
  heap->active_allocations = 0;
  heap->total_allocations = 0;
  heap->failed_allocations = 0;
  heap->free_list = nullptr;
  return heap->virtual_start < heap->virtual_limit;
}

void* heap_alloc(KernelHeap* heap, size_t size, size_t alignment) {
  if (heap == nullptr || heap->page_allocator == nullptr || size == 0) {
    record_failed_allocation(heap);
    return nullptr;
  }

  if (alignment == 0) {
    alignment = 1;
  }

  if (!is_power_of_two(alignment)) {
    record_failed_allocation(heap);
    return nullptr;
  }

  if (alignment < kHeapAlignment) {
    alignment = kHeapAlignment;
  }

  const uint64_t payload_bytes =
      align_up(static_cast<uint64_t>(size), kHeapAlignment);
  if (payload_bytes < size) {
    record_failed_allocation(heap);
    return nullptr;
  }

  while (true) {
    KernelHeapFreeRegion* previous = nullptr;
    KernelHeapFreeRegion* current = heap->free_list;

    while (current != nullptr) {
      void* allocation = nullptr;
      if (try_allocate_from_region(heap, current, previous, payload_bytes,
                                   static_cast<uint64_t>(alignment),
                                   &allocation)) {
        memory_set(allocation, 0, size);    // 对调用者仍然保持“新拿到的内存先清零”。
        ++heap->active_allocations;
        ++heap->total_allocations;
        return allocation;
      }

      previous = current;
      current = current->next;
    }

    if (!ensure_heap_mapping(
            heap,
            align_up(minimum_growth_bytes(payload_bytes, alignment),
                     kPagingPageSize))) {
      record_failed_allocation(heap);
      return nullptr;
    }
  }
}

bool heap_free(KernelHeap* heap, void* allocation) {
  if (heap == nullptr) {
    return false;
  }

  if (allocation == nullptr) {
    return true;   // 和 C 里的 free(nullptr) 一样，直接当成无事发生。
  }

  const uint64_t payload_start = address_of(allocation);
  if (payload_start < heap->virtual_start + sizeof(KernelHeapAllocationHeader) ||
      payload_start >= heap->mapped_limit) {
    return false;
  }

  auto* const header = reinterpret_cast<KernelHeapAllocationHeader*>(
      static_cast<uintptr_t>(payload_start - sizeof(KernelHeapAllocationHeader)));
  if (header->magic != kAllocationMagic) {
    return false;
  }

  if (header->payload_bytes > heap->used_bytes) {
    return false;
  }

  const uint64_t region_start = header->region_start;
  const uint64_t region_bytes = header->region_bytes;
  heap->used_bytes -= header->payload_bytes;
  if (heap->active_allocations != 0) {
    --heap->active_allocations;
  }
  header->magic = 0;
  insert_free_region(heap, region_start, region_bytes);
  return true;
}

uint64_t heap_used_bytes(const KernelHeap* heap) {
  if (heap == nullptr) {
    return 0;
  }

  return heap->used_bytes;
}

uint64_t heap_mapped_bytes(const KernelHeap* heap) {
  if (heap == nullptr || heap->mapped_limit < heap->virtual_start) {
    return 0;
  }

  return heap->mapped_limit - heap->virtual_start;
}

uint64_t heap_free_bytes(const KernelHeap* heap) {
  if (heap == nullptr) {
    return 0;
  }

  uint64_t total = 0;
  for (KernelHeapFreeRegion* region = heap->free_list; region != nullptr;
       region = region->next) {
    total += region->region_bytes;
  }

  return total;
}

uint64_t heap_active_allocations(const KernelHeap* heap) {
  if (heap == nullptr) {
    return 0;
  }

  return heap->active_allocations;
}

uint64_t heap_total_allocations(const KernelHeap* heap) {
  if (heap == nullptr) {
    return 0;
  }

  return heap->total_allocations;
}

uint64_t heap_failed_allocations(const KernelHeap* heap) {
  if (heap == nullptr) {
    return 0;
  }

  return heap->failed_allocations;
}
