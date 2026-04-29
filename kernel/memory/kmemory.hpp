#ifndef OS64_KMEMORY_HPP
#define OS64_KMEMORY_HPP

#include <stddef.h>
#include <stdint.h>

#include "memory/heap.hpp"
#include "memory/page_allocator.hpp"

// 这一层是“更正式的内核内存入口”：
// 上面模块不再直接拿着 KernelHeap 指针到处传，
// 而是先通过 kmalloc/kfree/knew/kdelete 这些更像正式内核接口的名字来用。
bool initialize_kernel_memory_system(PageAllocator* page_allocator,
                                     KernelHeap* heap);
bool kernel_memory_system_ready();
PageAllocator* kernel_memory_page_allocator();
KernelHeap* kernel_memory_heap();

void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, size_t alignment);
void* kcalloc(size_t count, size_t size);
bool kfree(void* allocation);

// freestanding 内核里没有标准库的 std::forward，
// 这里自己补一个最小版，只服务于 knew<T>(args...)。
template <typename T>
struct KRemoveReference {
  using Type = T;
};

template <typename T>
struct KRemoveReference<T&> {
  using Type = T;
};

template <typename T>
struct KRemoveReference<T&&> {
  using Type = T;
};

template <typename T>
constexpr typename KRemoveReference<T>::Type&& kforward(
    typename KRemoveReference<T>::Type& value) {
  return static_cast<typename KRemoveReference<T>::Type&&>(value);
}

template <typename T>
constexpr typename KRemoveReference<T>::Type&& kforward(
    typename KRemoveReference<T>::Type&& value) {
  return static_cast<typename KRemoveReference<T>::Type&&>(value);
}

// 标准 placement new 在宿主环境通常由 <new> 提供；
// 这里自己补最小声明，让内核也能在“已经分到的原始内存”上调用构造函数。
inline void* operator new(size_t, void* place) noexcept {
  return place;
}

inline void operator delete(void*, void*) noexcept {
}

template <typename T, typename... Args>
T* knew(Args&&... args) {
  void* const storage = kmalloc_aligned(sizeof(T), alignof(T));
  if (storage == nullptr) {
    return nullptr;
  }

  return new (storage) T(kforward<Args>(args)...);
}

template <typename T>
bool kdelete(T* object) {
  if (object == nullptr) {
    return true;
  }

  object->~T();
  return kfree(static_cast<void*>(object));
}

#endif
