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
// 判断 `kmalloc/kfree/knew` 这一整套入口是否已经能安全使用。
bool kernel_memory_system_ready();
// 需要时把“底层物理页分配器”暴露给别的模块观察。
PageAllocator* kernel_memory_page_allocator();
// 需要时把“默认内核堆对象”暴露给别的模块观察。
KernelHeap* kernel_memory_heap();

// 下面是更像正式内核 API 的入口：
// - `kmalloc`：分一块原始内存
// - `kmalloc_aligned`：分一块指定对齐的原始内存
// - `kcalloc`：分一块按元素数量计的清零内存
// - `kfree`：释放它
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

  // 先拿到一块“只是字节”的原始内存，
  // 再在这块内存上手动调用构造函数，真正变成一个 T 对象。
  return new (storage) T(kforward<Args>(args)...);
}

template <typename T>
bool kdelete(T* object) {
  if (object == nullptr) {
    return true;
  }

  // `kdelete` 要做两件事：
  // 1. 手动跑析构函数
  // 2. 再把承载对象的那块堆内存释放回去
  object->~T();
  return kfree(static_cast<void*>(object));
}

#endif
