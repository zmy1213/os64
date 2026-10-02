#ifndef OS64_PAGE_ALLOCATOR_HPP
#define OS64_PAGE_ALLOCATOR_HPP

#include <stdint.h>

#include "boot/boot_info.hpp"

constexpr uint64_t kPageSize = 4096;
// bootloader、内核文件字节和启动卷在低 1 MiB 内；这一段从不交给分配器。
constexpr uint64_t kAllocatorMinAddress = 0x100000;
// 与 linker.ld 的独立 BSS 窗口一致；硬件 E820 的 usable 不等于可分配。
constexpr uint64_t kAllocatorKernelDataBase = 0x100000;
constexpr uint64_t kAllocatorKernelDataLimit = 0x160000;
// entry64.asm 的内核启动栈从 0x180000 向下长，单独保留 64 KiB。
constexpr uint64_t kAllocatorBootStackBase = 0x170000;
constexpr uint64_t kAllocatorBootStackLimit = 0x180000;
// 教程先管理低 256 MiB。机器有更多内存时，超出的部分暂时不会被使用。
constexpr uint64_t kAllocatorManagedLimit = 256ULL * 1024 * 1024;
constexpr uint64_t kAllocatorPageCount = kAllocatorManagedLimit / kPageSize;
constexpr uint64_t kAllocatorBitmapWords = kAllocatorPageCount / 64;
constexpr uint16_t kMaxUsableRanges = 16;

struct PageAllocatorRange {
  // 仅供启动日志展示；真正的分配状态保存在下面的位图里。
  uint64_t next_free;
  uint64_t limit;
};

struct PageAllocator {
  PageAllocatorRange ranges[kMaxUsableRanges];
  uint16_t range_count;
  uint16_t active_range;
  uint64_t free_page_count;
  // 一个位代表一张物理页：available=可分配，allocated=已借给调用者。
  // 两个位图共 16 KiB，不必把链表指针写入待回收页，也能拒绝重复释放。
  uint64_t available_pages[kAllocatorBitmapWords];
  uint64_t allocated_pages[kAllocatorBitmapWords];
};

// 读取 E820；只有完整位于 usable 区间的页才可用。保留区与 usable 重叠时保留优先。
bool initialize_page_allocator(PageAllocator* allocator, const BootInfo* boot_info);

// 返回的是物理地址，不是可直接解引用的 C++ 指针。访问时使用 paging_physical_pointer。
// 页分配器本身只修改位图，所以在 direct map 建立之前也可以分配 bootstrap 页表。
uint64_t alloc_page(PageAllocator* allocator);
uint64_t alloc_page_below(PageAllocator* allocator, uint64_t limit);
uint64_t alloc_page_at_least(PageAllocator* allocator, uint64_t minimum);
bool free_page(PageAllocator* allocator, uint64_t physical_address);
bool page_allocator_owns_page(const PageAllocator* allocator, uint64_t physical_address);
uint64_t count_free_pages(const PageAllocator* allocator);

#endif
