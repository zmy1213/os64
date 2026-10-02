// 位图分配器只记录物理地址，不访问这些地址，所以可以在宿主机验证边界和回收。
// 真正的页表切换、direct map 和 NX 则必须由 QEMU 中的内核测试验证。
#include <cstdlib>
#include <iostream>
#include <vector>

#include "memory/page_allocator.hpp"

static void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static BootInfo boot_info_for(std::vector<E820Entry>& entries) {
  BootInfo info{};
  info.magic = kBootInfoMagic;
  info.memory_map_ptr = reinterpret_cast<uint64_t>(entries.data());
  info.memory_map_count = static_cast<uint16_t>(entries.size());
  info.memory_map_entry_size = sizeof(E820Entry);
  return info;
}

static void overlap_and_reservation() {
  std::vector<E820Entry> entries = {
      {0, 0x400000, 1, 1},
      {0x100000, 0x300000, 1, 1},  // 重叠 usable 不应重复记账。
      {0x180123, 1, 2, 1},         // 部分保留区也排除整页。
      {0x200000, 0x1000, 1, 0},   // E820 扩展属性标成 disabled。
  };
  BootInfo info = boot_info_for(entries);
  info.boot_volume_ptr = 0x300123;
  info.boot_volume_sector_count = 9;
  info.boot_volume_sector_size = 512;  // 卷覆盖两页。
  PageAllocator allocator;
  require(initialize_page_allocator(&allocator, &info), "initialize overlap map");
  require(count_free_pages(&allocator) == 768 - 4 - 16, "exclude reserved, stack and boot pages once");
  std::vector<uint64_t> pages;
  for (uint64_t page = alloc_page(&allocator); page != 0; page = alloc_page(&allocator)) {
    require(page >= 0x100000 && page < 0x400000, "protect low boot memory");
    require(page < kAllocatorBootStackBase || page >= kAllocatorBootStackLimit,
            "protect live bootstrap kernel stack");
    require(page != 0x180000 && page != 0x200000 && page != 0x300000 && page != 0x301000,
            "reservation wins regardless of usable overlap");
    pages.push_back(page);
  }
  require(pages.size() == 748 && count_free_pages(&allocator) == 0, "exact ENOSPC count");
  require(!free_page(&allocator, 0x180000), "cannot free reserved page");
  require(!free_page(&allocator, pages[0] + 1), "cannot free unaligned address");
  for (uint64_t page : pages) {
    require(free_page(&allocator, page), "return allocated pages");
    require(!free_page(&allocator, page), "reject double free");
  }
  require(count_free_pages(&allocator) == 748, "all pages reclaimed");
}

static void high_pages_and_limit() {
  std::vector<E820Entry> entries = {
      {0x100000, 0x100000, 1, 1},
      {32ULL * 1024 * 1024, 512ULL * 1024 * 1024, 1, 1},
      {UINT64_MAX - 0x1000, 0x3000, 1, 1},  // 不应溢出后绕回低地址。
      {kAllocatorManagedLimit - 0x1800, UINT64_MAX, 2, 1},
  };
  BootInfo info = boot_info_for(entries);
  PageAllocator allocator;
  require(initialize_page_allocator(&allocator, &info), "initialize clipped high map");
  const uint64_t initial = count_free_pages(&allocator);
  require(initial == 256 - 16 + (224ULL * 1024 * 1024 / kPageSize) - 2,
          "clip to 256 MiB and reserve overflow tail");
  require(alloc_page_below(&allocator, 0x100000) == 0, "below minimum fails");
  require(alloc_page_below(&allocator, 0x101FFF) == 0x100000, "limit covers one complete page");
  require(alloc_page_below(&allocator, 0x101FFF) == 0, "do not return partial boundary page");
  const uint64_t high = alloc_page_at_least(&allocator, 32ULL * 1024 * 1024 + 1);
  require(high == 32ULL * 1024 * 1024 + kPageSize, "align high minimum upward");
  require(page_allocator_owns_page(&allocator, high), "own high allocated page");
  require(free_page(&allocator, high) && !free_page(&allocator, high), "reclaim high and reject repeat");
  require(alloc_page_at_least(&allocator, high) == high, "reuse high page");
  require(alloc_page_at_least(&allocator, UINT64_MAX) == 0, "reject minimum overflow");
  require(alloc_page_at_least(&allocator, kAllocatorManagedLimit) == 0, "reject managed upper boundary");
  require(!free_page(&allocator, 0x1000) &&
          !free_page(&allocator, kAllocatorManagedLimit) &&
          !free_page(&allocator, 8ULL * 1024 * 1024), "reject foreign and unavailable pages");
  require(count_free_pages(&allocator) == initial - 2, "free count follows live ownership");
}

static void boundary_and_invalid_maps() {
  std::vector<E820Entry> entries = {{0x100001, 0x3FFE, 1, 1}};
  BootInfo info = boot_info_for(entries);
  PageAllocator allocator;
  require(initialize_page_allocator(&allocator, &info), "initialize partial page map");
  require(count_free_pages(&allocator) == 2, "usable rounding includes only complete pages");
  require(alloc_page(&allocator) == 0x101000 && alloc_page(&allocator) == 0x102000 &&
          alloc_page(&allocator) == 0, "bounded allocations reach ENOSPC");
  info.memory_map_entry_size = 20;
  require(!initialize_page_allocator(&allocator, &info), "reject incompatible E820 layout");
  require(count_free_pages(&allocator) == 0, "invalid initialization clears ownership state");
  info.memory_map_entry_size = sizeof(E820Entry);
  info.memory_map_count = kMaxUsableRanges + 1;
  require(!initialize_page_allocator(&allocator, &info), "reject out of boot E820 buffer bounds");
  require(!initialize_page_allocator(nullptr, &info) && !initialize_page_allocator(&allocator, nullptr),
          "reject null map arguments");
}

int main() {
  overlap_and_reservation();
  high_pages_and_limit();
  boundary_and_invalid_maps();
  std::cout << "page allocator host tests passed: E820, high pages, ownership, ENOSPC\n";
}
