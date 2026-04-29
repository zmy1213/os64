#include "memory/kmemory.hpp"

namespace {

constexpr size_t kKernelDefaultObjectAlignment = 16;  // 和 heap 当前的最小对齐保持一致。

struct KernelMemoryState {
  PageAllocator* page_allocator;  // 让高层代码必要时还能拿到底层页分配器。
  KernelHeap* heap;               // 真正承接 kmalloc/kfree 的默认内核堆。
  bool ready;                     // 只有初始化成功后，上层才允许开始分配对象。
};

KernelMemoryState g_kernel_memory = {
    nullptr,
    nullptr,
    false,
};

bool multiply_would_overflow(size_t left, size_t right) {
  if (left == 0 || right == 0) {
    return false;
  }

  return left > (static_cast<size_t>(-1) / right);
}

}  // namespace

bool initialize_kernel_memory_system(PageAllocator* page_allocator,
                                     KernelHeap* heap) {
  if (page_allocator == nullptr || heap == nullptr) {
    return false;
  }

  g_kernel_memory.page_allocator = page_allocator;
  g_kernel_memory.heap = heap;
  g_kernel_memory.ready = true;
  return true;
}

bool kernel_memory_system_ready() {
  return g_kernel_memory.ready && g_kernel_memory.page_allocator != nullptr &&
         g_kernel_memory.heap != nullptr;
}

PageAllocator* kernel_memory_page_allocator() {
  if (!kernel_memory_system_ready()) {
    return nullptr;
  }

  return g_kernel_memory.page_allocator;
}

KernelHeap* kernel_memory_heap() {
  if (!kernel_memory_system_ready()) {
    return nullptr;
  }

  return g_kernel_memory.heap;
}

void* kmalloc(size_t size) {
  return kmalloc_aligned(size, kKernelDefaultObjectAlignment);
}

void* kmalloc_aligned(size_t size, size_t alignment) {
  KernelHeap* const heap = kernel_memory_heap();
  if (heap == nullptr) {
    return nullptr;
  }

  return heap_alloc(heap, size, alignment);
}

void* kcalloc(size_t count, size_t size) {
  if (count == 0 || size == 0 || multiply_would_overflow(count, size)) {
    return nullptr;
  }

  return kmalloc(count * size);  // heap_alloc 已经会把返回给调用者的区域清零。
}

bool kfree(void* allocation) {
  KernelHeap* const heap = kernel_memory_heap();
  if (heap == nullptr) {
    return false;
  }

  return heap_free(heap, allocation);
}
