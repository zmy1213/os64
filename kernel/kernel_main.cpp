#include <stddef.h>
#include <stdint.h>

#include "boot_info.hpp"
#include "page_allocator.hpp"
#include "paging.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;          // VGA 文本模式一行 80 列。
constexpr uintptr_t kVgaBase = 0xB8000;       // VGA 文本缓冲区从这个物理地址开始。
constexpr uint16_t kCom1Base = 0x3F8;         // COM1 串口的标准 I/O 端口基址。
constexpr uint8_t kTextColor = 0x0F;          // 白字黑底，和前面的 stage2 风格保持一致。
constexpr uint16_t kMaxDecimalDigits = 20;    // 打印 64 位十进制时最多不会超过 20 位。
constexpr uint64_t kPagingTestVirtualAddress = 0x0000000000200000ULL;  // 2 MiB，正好落在当前临时映射之外。
constexpr uint64_t kPagingTestPattern = 0x1122334455667788ULL;         // 用一个好认的模式值验证读写。

PageAllocator g_page_allocator;               // 第一版物理页分配器状态先放在一个全局对象里。

// 往 I/O 端口写一个字节。内核里没有现成库函数，所以这里自己直接发机器指令。
inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// 从 I/O 端口读一个字节。串口状态轮询要靠它。
inline uint8_t in8(uint16_t port) {
  uint8_t value = 0;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

// 串口发送前先等发送保持寄存器空出来。
void serial_write_char(char ch) {
  while ((in8(kCom1Base + 5) & 0x20) == 0) {
  }

  out8(kCom1Base, static_cast<uint8_t>(ch));
}

// 把一个 0 结尾字符串送到串口，方便自动测试直接抓日志。
void serial_write_string(const char* text) {
  for (size_t i = 0; text[i] != '\0'; ++i) {
    serial_write_char(text[i]);
  }
}

// 换行在串口里要写 CRLF，这样终端和日志都更稳定。
void serial_write_crlf() {
  serial_write_string("\r\n");
}

// 打印 1 个十六进制字符，比如 10 -> 'A'。
void serial_write_hex_nibble(uint8_t value) {
  if (value < 10) {
    serial_write_char(static_cast<char>('0' + value));
    return;
  }

  serial_write_char(static_cast<char>('A' + (value - 10)));
}

// 固定打印 16 个十六进制字符，适合拿来看物理地址。
void serial_write_hex64(uint64_t value) {
  for (int shift = 60; shift >= 0; shift -= 4) {
    const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xF);
    serial_write_hex_nibble(nibble);
  }
}

// 十进制打印主要用来显示“第几条 E820 记录”和“还有多少页”。
void serial_write_u64(uint64_t value) {
  char digits[kMaxDecimalDigits];
  size_t count = 0;

  if (value == 0) {
    serial_write_char('0');
    return;
  }

  while (value != 0 && count < kMaxDecimalDigits) {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }

  while (count > 0) {
    serial_write_char(digits[--count]);
  }
}

// 直接往 VGA 文本缓冲区写一整行，让你在 QEMU 图形窗口里也能看到结果。
void vga_write_line(uint16_t row, const char* text, uint8_t color) {
  volatile uint16_t* const vga =
      reinterpret_cast<volatile uint16_t*>(kVgaBase);

  for (size_t col = 0; col < kVgaColumns; ++col) {
    const char ch = text[col];
    if (ch == '\0') {
      break;
    }

    vga[row * kVgaColumns + col] =
        static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(ch);
  }
}

// 既写屏幕也写串口，这样手工看和自动测都能兼顾。
void write_status_line(uint16_t row, const char* text) {
  vga_write_line(row, text, kTextColor);
  serial_write_string(text);
  serial_write_crlf();
}

// 先只做最重要的判断：type=1 就当 usable，其它先统一当 reserved。
const char* memory_kind_name(uint32_t type) {
  if (type == kE820TypeUsable) {
    return "usable";
  }

  return "reserved";
}

// 判断 BootInfo 是否至少长得像我们约定的结构，避免一上来就乱解指针。
bool is_boot_info_valid(const BootInfo* boot_info) {
  return boot_info != nullptr && boot_info->magic == kBootInfoMagic &&
         boot_info->memory_map_ptr != 0 &&
         boot_info->memory_map_entry_size == sizeof(E820Entry);
}

// 一条一条把 E820 记录打印出来，让你真正看到 BIOS 给了内核什么内存地图。
void log_e820_entries(const BootInfo* boot_info) {
  const auto* entries =
      reinterpret_cast<const E820Entry*>(static_cast<uintptr_t>(boot_info->memory_map_ptr));

  for (uint16_t i = 0; i < boot_info->memory_map_count; ++i) {
    const E820Entry& entry = entries[i];

    serial_write_string("e820[");
    serial_write_u64(i);
    serial_write_string("] base=0x");
    serial_write_hex64(entry.base);
    serial_write_string(" length=0x");
    serial_write_hex64(entry.length);
    serial_write_string(" raw_type=0x");
    serial_write_hex64(entry.type);
    serial_write_string(" kind=");
    serial_write_string(memory_kind_name(entry.type));
    serial_write_crlf();
  }
}

// 把真正收进页分配器的 usable 区间再单独打印一遍。
// 这样你能看见：不是所有 E820 usable 条目都会原样进入分配器，低地址会被故意裁掉。
void log_allocator_ranges(const PageAllocator& allocator) {
  for (uint16_t i = 0; i < allocator.range_count; ++i) {
    const PageAllocatorRange& range = allocator.ranges[i];
    serial_write_string("allocator_range[");
    serial_write_u64(i);
    serial_write_string("] start=0x");
    serial_write_hex64(range.next_free);
    serial_write_string(" end=0x");
    serial_write_hex64(range.limit);
    serial_write_crlf();
  }
}

// 先试着连续拿 3 个页，让结果一眼可见。
void log_allocated_pages(PageAllocator* allocator) {
  for (uint16_t i = 0; i < 3; ++i) {
    const uint64_t page = alloc_page(allocator);
    serial_write_string("alloc_page_");
    serial_write_u64(i);
    serial_write_string("=0x");
    serial_write_hex64(page);
    serial_write_crlf();
  }
}

// 这一步才是真正把“物理页分配器”和“页表管理器”接起来：
// 先拿到一个物理页，再把它映射到一个新的虚拟地址，然后实际写进去读出来。
bool run_paging_smoke_test(PageAllocator* allocator) {
  if (allocator == nullptr) {
    return false;
  }

  const uint64_t mapped_physical_page = alloc_page(allocator);
  if (mapped_physical_page == 0) {
    return false;
  }

  serial_write_string("mapped_test_page=0x");
  serial_write_hex64(mapped_physical_page);
  serial_write_crlf();

  if (!map_page(allocator, kPagingTestVirtualAddress, mapped_physical_page,
                kPageWritable)) {
    return false;
  }

  serial_write_string("mapped_virtual=0x");
  serial_write_hex64(kPagingTestVirtualAddress);
  serial_write_crlf();

  auto* const mapped_value =
      reinterpret_cast<volatile uint64_t*>(static_cast<uintptr_t>(kPagingTestVirtualAddress));
  *mapped_value = kPagingTestPattern;
  const uint64_t read_back = *mapped_value;

  serial_write_string("read_back_value=0x");
  serial_write_hex64(read_back);
  serial_write_crlf();

  return read_back == kPagingTestPattern;
}

}  // namespace

extern "C" void kernel_main(const BootInfo* boot_info) {
  write_status_line(4, "hello from os64 kernel");

  if (!is_boot_info_valid(boot_info)) {
    write_status_line(5, "boot info bad");
    return;
  }

  write_status_line(5, "boot info ok");

  log_e820_entries(boot_info);                 // 第一步：真正把 BIOS 给的内存地图读出来。
  write_status_line(6, "e820 parse ok");

  if (!initialize_page_allocator(&g_page_allocator, boot_info)) {
    write_status_line(7, "page allocator bad");
    return;
  }

  log_allocator_ranges(g_page_allocator);      // 第二步：把 usable 区域裁成真正可分配的页区间。
  log_allocated_pages(&g_page_allocator);      // 第三步：实际分几个页，证明 alloc_page() 已经能用了。

  serial_write_string("free_pages_remaining=");
  serial_write_u64(count_free_pages(&g_page_allocator));
  serial_write_crlf();

  write_status_line(7, "page allocator ok");

  if (!run_paging_smoke_test(&g_page_allocator)) {
    write_status_line(8, "map_page bad");
    return;
  }

  write_status_line(8, "map_page ok");
}
