#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.hpp"
#include "console/console.hpp"
#include "interrupts/interrupts.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pic.hpp"
#include "interrupts/pit.hpp"
#include "memory/heap.hpp"
#include "memory/page_allocator.hpp"
#include "memory/paging.hpp"
#include "shell/shell.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;          // VGA 文本模式一行 80 列。
constexpr uintptr_t kVgaBase = 0xB8000;       // VGA 文本缓冲区从这个物理地址开始。
constexpr uint16_t kCom1Base = 0x3F8;         // COM1 串口的标准 I/O 端口基址。
constexpr uint8_t kStatusTextColor = 0x07;    // 浅灰字黑底，比纯白更轻，不会显得那么“顶眼”。
constexpr uint8_t kShellTextColor = 0x07;     // shell 正文也统一用浅灰色，减轻整屏发白的感觉。
constexpr uint8_t kShellPromptColor = 0x03;   // 提示符改成柔一点的青色，让交互入口更清楚但不刺眼。
constexpr uint8_t kExceptionTextColor = 0x0C; // 异常标题保留醒目的浅红色，方便一眼看出是错误路径。
constexpr uint16_t kMaxDecimalDigits = 20;    // 打印 64 位十进制时最多不会超过 20 位。
constexpr uint64_t kPagingTestVirtualAddress = 0x0000000000200000ULL;  // 2 MiB，正好落在当前临时映射之外。
constexpr uint64_t kPagingTestPattern = 0x1122334455667788ULL;         // 用一个好认的模式值验证读写。
constexpr uint64_t kHeapTestSmallPattern = 0xA1B2C3D4E5F60718ULL;      // 小块堆分配测试用的模式值。
constexpr uint64_t kHeapTestLargePattern0 = 0x0123456789ABCDEFULL;     // 大块堆分配第一页起始位置的模式值。
constexpr uint64_t kHeapTestLargePattern1 = 0xFEDCBA9876543210ULL;     // 大块堆分配跨页位置的模式值。
constexpr size_t kHeapTestSmallSize = 64;                              // 第一块堆内存，故意做得很小。
constexpr size_t kHeapTestLargeSize = 6000;                            // 第二块堆内存，故意让它跨过一个 4 KiB 页边界。
constexpr size_t kHeapReuseSize = 32;                                  // 释放小块后，再申请一个更小块，测试是否复用。
constexpr size_t kHeapMergeBlockASize = 512;                           // 合并测试里的第一个相邻块。
constexpr size_t kHeapMergeBlockBSize = 768;                           // 合并测试里的第二个相邻块。
constexpr size_t kHeapMergedRequestSize = 1024;                        // 如果 A/B 真合并了，这个请求应该能从 A 位置拿到。
constexpr uint32_t kPitFrequencyHz = 100;                              // 先把时钟中断频率设成 100Hz，够平滑也够好测。
constexpr uint64_t kTimerFirstLogTick = 10;                            // 先在第 10 个 tick 打一次点。
constexpr uint64_t kTimerSecondLogTick = 20;                           // 第 20 个 tick 再打一次点，证明中断在持续发生。
constexpr uint64_t kTimerWaitTestTicks = 3;                            // 第一段先直接按 tick 等 3 下。
constexpr uint64_t kTimerSleepTestMs = 50;                             // 第二段再按毫秒等 50ms，在 100Hz 下约等于 5 个 tick。
constexpr uint64_t kTimerSleepMinTicks = 5;                            // 50ms 在 100Hz 下至少应该跨过 5 个 tick。
constexpr uint8_t kKeyboardIrqLine = 1;                                // 传统 PC 键盘走主 PIC 的 IRQ1。
constexpr uint64_t kKeyboardTestTimeoutTicks = 20;                     // 键盘测试每轮最多等 20 个 tick，避免异常时无限卡住。
constexpr uint16_t kConsoleTestStartRow = 16;                          // 把控制台测试区域放到更下面，避免覆盖前面的状态行。
constexpr size_t kConsoleLineBufferCapacity = 32;                      // 这一轮测试只读短行，32 字节足够验证流程。
constexpr uint16_t kShellTestStartRow = 16;                            // shell 也复用下半屏区域，避免和状态行互相覆盖。
constexpr size_t kShellLineBufferCapacity = 32;                        // `help`/`ticks` 这种命令很短，32 字节够第一版 shell 用。
constexpr uint8_t kKeyboardTestScancodes[] = {
    0x1E,  // A 按下 -> 'a'
    0x9E,  // A 松开 -> 这一轮应忽略，不进入字符缓冲区
    0x30,  // B 按下 -> 'b'
    0x02,  // 1 按下 -> '1'
    0x39,  // Space 按下 -> ' '
    0x1C,  // Enter 按下 -> '\n'
    0x0E,  // Backspace 按下 -> '\b'
};
constexpr char kKeyboardExpectedChars[] = {
    'a',
    'b',
    '1',
    ' ',
    '\n',
    '\b',
};
constexpr uint8_t kConsoleInputTestScancodes[] = {
    0x18,  // O -> 'o'
    0x1F,  // S -> 's'
    0x39,  // Space -> ' '
    0x07,  // 6 -> '6'
    0x06,  // 5 -> '5'
    0x0E,  // Backspace -> 删除刚才的 '5'
    0x05,  // 4 -> '4'
    0x1C,  // Enter -> 结束这一行
};
constexpr char kConsoleExpectedLine[] = "os 64";                       // 这就是退格修正后的最终结果。
constexpr uint8_t kShellHelpScancodes[] = {
    0x23, 0x12, 0x26, 0x19, 0x1C,  // help + Enter
};
constexpr uint8_t kShellMemScancodes[] = {
    0x32, 0x12, 0x32, 0x1C,        // mem + Enter
};
constexpr uint8_t kShellTicksScancodes[] = {
    0x14, 0x17, 0x2E, 0x25, 0x1F, 0x1C,  // ticks + Enter
};
constexpr uint8_t kShellHeapScancodes[] = {
    0x23, 0x12, 0x1E, 0x19, 0x1C,  // heap + Enter
};
constexpr uint8_t kShellIrqScancodes[] = {
    0x17, 0x13, 0x10, 0x1C,        // irq + Enter
};
constexpr uint8_t kShellBootInfoScancodes[] = {
    0x30, 0x18, 0x18, 0x14, 0x17, 0x31, 0x21, 0x18, 0x1C,  // bootinfo + Enter
};
constexpr uint8_t kShellE820Scancodes[] = {
    0x12, 0x09, 0x03, 0x0B, 0x1C,  // e820 + Enter
};
constexpr uint8_t kShellCpuScancodes[] = {
    0x2E, 0x19, 0x16, 0x1C,        // cpu + Enter
};
constexpr uint8_t kShellEditedEchoScancodes[] = {
    0x2D, 0x12, 0x2E, 0x23, 0x18, 0x39, 0x23, 0x17, 0x05, 0x03, 0x2C,
    0xE0, 0x47,                    // Home
    0xE0, 0x4D,                    // Right
    0xE0, 0x4B,                    // Left
    0xE0, 0x53,                    // Delete
    0xE0, 0x4F,                    // End
    0xE0, 0x4B,                    // Left
    0xE0, 0x4D,                    // Right
    0x0E,                          // Backspace
    0x1C,                          // Enter
};
constexpr uint8_t kShellUptimeScancodes[] = {
    0x16, 0x19, 0x14, 0x17, 0x32, 0x12, 0x1C,  // uptime + Enter
};
constexpr uint8_t kShellHistoryScancodes[] = {
    0x23, 0x17, 0x1F, 0x14, 0x18, 0x13, 0x15, 0x1C,  // history + Enter
};
constexpr uint8_t kShellHistoryBrowseScancodes[] = {
    0xE0, 0x48,                    // Up
    0xE0, 0x48,                    // Up
    0xE0, 0x50,                    // Down
    0x1C,                          // Enter
};
constexpr uint8_t kShellClearScancodes[] = {
    0x2E, 0x26, 0x12, 0x1E, 0x13, 0x1C,  // clear + Enter
};
constexpr uint8_t kShellBadScancodes[] = {
    0x30, 0x1E, 0x20, 0x1C,        // bad + Enter
};
#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
constexpr uint64_t kInvalidOpcodeMarker = 0x55443231494E5641ULL;       // 只是为了日志里有个好认的常量。
#endif
#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
constexpr uint64_t kPageFaultSmokeAddress = 0x0000000000900000ULL;     // 9 MiB，这一轮没有映射它，专门拿来触发页错误。
constexpr uint64_t kPageFaultSmokePattern = 0x0BADF00DCAFED00DULL;     // 页错误测试里尝试写入的值。
#endif

PageAllocator g_page_allocator;               // 第一版物理页分配器状态先放在一个全局对象里。
KernelHeap g_kernel_heap;                     // 第一版内核堆状态也先放成全局对象，方便后面各模块共享。
ShellState g_shell;                           // 最小 shell 的状态也先放在全局对象里，后面交互循环会一直复用。

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
  // 这里故意拆成两个单字符发送，而不是再走一遍字符串遍历。
  // 这样这一层的行为更直接，也更容易排查最早期串口日志问题。
  serial_write_char('\r');
  serial_write_char('\n');
}

// shell 的输出需要同时进 VGA 控制台和串口日志。
// 这样你手工运行时能看见提示符，自动测试时也能抓到命令结果。
void shell_output_char(char ch) {
  console_write_char(ch);

  if (ch == '\n') {
    serial_write_crlf();
    return;
  }

  serial_write_char(ch);
}

void shell_clear_output() {
  console_clear();
}

void shell_set_output_color(uint8_t color) {
  console_set_color(color);
}

size_t shell_history_provider_count(const void* context) {
  return shell_history_entry_count(static_cast<const ShellState*>(context));
}

const char* shell_history_provider_text(const void* context, size_t index) {
  return shell_history_entry_text(static_cast<const ShellState*>(context),
                                  index);
}

const ShellOutput kShellOutput = {
    shell_output_char,
    shell_clear_output,
    shell_set_output_color,
    kShellTextColor,
    kShellPromptColor,
};

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

bool is_aligned(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return false;
  }

  return (value & (alignment - 1)) == 0;
}

size_t string_length(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

bool strings_equal(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }

  for (size_t i = 0;; ++i) {
    if (left[i] != right[i]) {
      return false;
    }

    if (left[i] == '\0') {
      return true;
    }
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
  vga_write_line(row, text, kStatusTextColor);
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

// 这里用两次堆分配证明两件事：
// 1. 小块对象已经能落在新的堆虚拟地址上
// 2. 大块对象已经能跨页工作，而不是只会在单页里凑巧成功
bool run_heap_smoke_test(KernelHeap* heap) {
  if (heap == nullptr) {
    return false;
  }

  auto* const small_block =
      static_cast<uint64_t*>(heap_alloc(heap, kHeapTestSmallSize, 16));
  if (small_block == nullptr) {
    return false;
  }

  serial_write_string("heap_small=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(small_block));
  serial_write_crlf();

  auto* const large_block =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapTestLargeSize,
                                       kPagingPageSize));
  if (large_block == nullptr) {
    return false;
  }

  serial_write_string("heap_large=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(large_block));
  serial_write_crlf();

  serial_write_string("heap_large_page_aligned=");
  serial_write_u64(is_aligned(reinterpret_cast<uint64_t>(large_block),
                              kPagingPageSize)
                       ? 1
                       : 0);
  serial_write_crlf();

  auto* const large_first_word =
      reinterpret_cast<uint64_t*>(large_block);
  auto* const large_cross_page_word =
      reinterpret_cast<uint64_t*>(large_block + kPagingPageSize);

  *small_block = kHeapTestSmallPattern;
  *large_first_word = kHeapTestLargePattern0;
  *large_cross_page_word = kHeapTestLargePattern1;

  const uint64_t small_read_back = *small_block;
  const uint64_t cross_page_read_back = *large_cross_page_word;

  serial_write_string("heap_cross_page_value=0x");
  serial_write_hex64(cross_page_read_back);
  serial_write_crlf();

  if (!heap_free(heap, small_block)) {
    return false;
  }

  auto* const reused_small =
      static_cast<uint64_t*>(heap_alloc(heap, kHeapReuseSize, 16));
  if (reused_small == nullptr) {
    return false;
  }

  serial_write_string("heap_reuse=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(reused_small));
  serial_write_crlf();

  auto* const merge_block_a =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergeBlockASize, 16));
  auto* const merge_block_b =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergeBlockBSize, 16));
  if (merge_block_a == nullptr || merge_block_b == nullptr) {
    return false;
  }

  if (!heap_free(heap, merge_block_a) || !heap_free(heap, merge_block_b)) {
    return false;
  }

  auto* const merged_block =
      static_cast<uint8_t*>(heap_alloc(heap, kHeapMergedRequestSize, 16));
  if (merged_block == nullptr) {
    return false;
  }

  serial_write_string("heap_coalesced=0x");
  serial_write_hex64(reinterpret_cast<uint64_t>(merged_block));
  serial_write_crlf();

  serial_write_string("heap_used_bytes=");
  serial_write_u64(heap_used_bytes(heap));
  serial_write_crlf();

  serial_write_string("heap_mapped_bytes=");
  serial_write_u64(heap_mapped_bytes(heap));
  serial_write_crlf();

  serial_write_string("heap_free_bytes=");
  serial_write_u64(heap_free_bytes(heap));
  serial_write_crlf();

  return small_read_back == kHeapTestSmallPattern &&
         cross_page_read_back == kHeapTestLargePattern1 &&
         is_aligned(reinterpret_cast<uint64_t>(large_block), kPagingPageSize) &&
         reused_small == small_block && merged_block == merge_block_a;
}

bool run_timer_smoke_test() {
  // 先初始化 PIC，让外部硬件中断有地方可去，并避开 CPU 异常向量区。
  if (!initialize_pic()) {
    return false;
  }

  serial_write_string("pic ok");
  serial_write_crlf();

  // 再初始化 PIT，让 IRQ0 真的开始按固定频率发生。
  if (!initialize_pit(kPitFrequencyHz)) {
    return false;
  }

  serial_write_string("pit ok");
  serial_write_crlf();

  serial_write_string("pit_frequency_hz=");
  serial_write_u64(timer_frequency_hz());
  serial_write_crlf();

  enable_interrupts();   // 到这里才真正允许外部硬件中断进来。

  bool logged_first_tick = false;
  bool logged_second_tick = false;

  while (!logged_second_tick) {
    // 不断看“全局 tick 计数器”现在走到了几。
    const uint64_t ticks = timer_tick_count();

    if (!logged_first_tick && ticks >= kTimerFirstLogTick) {
      serial_write_string("timer_tick=");
      serial_write_u64(ticks);
      serial_write_crlf();
      logged_first_tick = true;
    }

    if (ticks >= kTimerSecondLogTick) {
      serial_write_string("timer_tick=");
      serial_write_u64(ticks);
      serial_write_crlf();
      logged_second_tick = true;
      break;                        // 看到第 20 个 tick 就够证明这条 IRQ 链已经持续工作。
    }

    wait_for_interrupt();  // 没有到目标 tick 之前，就先让 CPU 睡到下一次中断再醒。
  }

  // 第一段：直接按 tick 等待，证明“系统已经能靠中断睡到未来某个 tick 再醒”。
  const uint64_t wait_start_tick = timer_tick_count();
  timer_wait_ticks(kTimerWaitTestTicks);
  const uint64_t wait_end_tick = timer_tick_count();
  const uint64_t wait_elapsed_ticks = wait_end_tick - wait_start_tick;

  serial_write_string("timer_wait_elapsed_ticks=");
  serial_write_u64(wait_elapsed_ticks);
  serial_write_crlf();

  // 第二段：再给一个更像用户视角的接口，按毫秒等待。
  const uint64_t sleep_start_tick = timer_tick_count();
  if (!timer_sleep_ms(kTimerSleepTestMs)) {
    disable_interrupts();
    return false;
  }
  const uint64_t sleep_end_tick = timer_tick_count();
  const uint64_t sleep_elapsed_ticks = sleep_end_tick - sleep_start_tick;

  serial_write_string("timer_sleep_ms=");
  serial_write_u64(kTimerSleepTestMs);
  serial_write_crlf();

  serial_write_string("timer_sleep_elapsed_ticks=");
  serial_write_u64(sleep_elapsed_ticks);
  serial_write_crlf();

  disable_interrupts();             // 测试结束先关掉中断，避免后面异常测试被时钟持续打断。
  return timer_is_ready() &&
         timer_frequency_hz() == kPitFrequencyHz &&
         wait_elapsed_ticks >= kTimerWaitTestTicks &&
         sleep_elapsed_ticks >= kTimerSleepMinTicks;
}

bool wait_for_keyboard_irq_count(uint64_t target_count,
                                 uint64_t timeout_ticks) {
  const uint64_t start_tick = timer_tick_count();

  while (keyboard_irq_count() < target_count) {
    if ((timer_tick_count() - start_tick) >= timeout_ticks) {
      return false;
    }

    wait_for_interrupt();
  }

  return true;
}

bool run_keyboard_smoke_test() {
  if (!initialize_keyboard()) {
    return false;
  }

  serial_write_string("keyboard init ok");
  serial_write_crlf();

  // 这一轮只单独放开 IRQ1，让键盘中断第一次真的能进入内核。
  if (!enable_pic_irq(kKeyboardIrqLine)) {
    return false;
  }

  serial_write_string("keyboard irq1 enabled");
  serial_write_crlf();

  const uint64_t before_irq_count = keyboard_irq_count();
  const uint64_t expected_irq_count =
      before_irq_count +
      (sizeof(kKeyboardTestScancodes) / sizeof(kKeyboardTestScancodes[0]));

  enable_interrupts();

  // 这一轮不再只测“来过一次 IRQ”，
  // 而是连续注入一串扫描码，验证：
  // 1. IRQ 顺序没有乱
  // 2. break code 会被忽略
  // 3. make code 会被翻译成字符并进入 FIFO 缓冲区
  for (uint64_t i = 0; i < (sizeof(kKeyboardTestScancodes) /
                            sizeof(kKeyboardTestScancodes[0]));
       ++i) {
    serial_write_string("keyboard_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kKeyboardTestScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kKeyboardTestScancodes[i])) {
      disable_interrupts();
      return false;
    }

    if (!wait_for_keyboard_irq_count(before_irq_count + i + 1,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();

  const uint64_t after_irq_count = keyboard_irq_count();
  const uint8_t last_scancode = keyboard_last_scancode();
  const uint16_t buffered_char_count = keyboard_buffered_char_count();
  const uint64_t dropped_char_count = keyboard_dropped_char_count();

  serial_write_string("keyboard_irq_count=");
  serial_write_u64(after_irq_count);
  serial_write_crlf();

  serial_write_string("keyboard_last_scancode=0x");
  serial_write_hex64(last_scancode);
  serial_write_crlf();

  serial_write_string("keyboard_char_count=");
  serial_write_u64(buffered_char_count);
  serial_write_crlf();

  bool chars_match = true;
  for (uint64_t i = 0; i < (sizeof(kKeyboardExpectedChars) /
                            sizeof(kKeyboardExpectedChars[0]));
       ++i) {
    char character = '\0';
    if (!keyboard_try_read_char(&character)) {
      chars_match = false;
      break;
    }

    serial_write_string("keyboard_char_");
    serial_write_u64(i);
    serial_write_string("=0x");
    serial_write_hex64(static_cast<uint8_t>(character));
    serial_write_crlf();

    if (character != kKeyboardExpectedChars[i]) {
      chars_match = false;
    }
  }

  char extra_character = '\0';
  const bool has_extra_char = keyboard_try_read_char(&extra_character);
  const uint16_t remaining_chars = keyboard_buffered_char_count();

  serial_write_string("keyboard_buffer_remaining=");
  serial_write_u64(remaining_chars);
  serial_write_crlf();

  serial_write_string("keyboard_dropped_chars=");
  serial_write_u64(dropped_char_count);
  serial_write_crlf();

  return after_irq_count == expected_irq_count &&
         last_scancode ==
             kKeyboardTestScancodes[(sizeof(kKeyboardTestScancodes) /
                                     sizeof(kKeyboardTestScancodes[0])) -
                                    1] &&
         buffered_char_count ==
             (sizeof(kKeyboardExpectedChars) / sizeof(kKeyboardExpectedChars[0])) &&
         chars_match && !has_extra_char && remaining_chars == 0 &&
         dropped_char_count == 0;
}

bool run_console_input_smoke_test() {
  if (keyboard_buffered_char_count() != 0) {
    return false;  // 先确认上一轮字符测试已经把缓冲区消费干净。
  }

  initialize_console(kConsoleTestStartRow, kShellTextColor);
  console_write_string("input> ");  // 这一轮先给最小控制台写一个提示符，模拟将来的命令行入口。

  const uint64_t before_irq_count = keyboard_irq_count();
  const uint64_t expected_irq_count =
      before_irq_count +
      (sizeof(kConsoleInputTestScancodes) /
       sizeof(kConsoleInputTestScancodes[0]));

  enable_interrupts();

  for (uint64_t i = 0; i < (sizeof(kConsoleInputTestScancodes) /
                            sizeof(kConsoleInputTestScancodes[0]));
       ++i) {
    serial_write_string("console_inject[");
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(kConsoleInputTestScancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(kConsoleInputTestScancodes[i])) {
      disable_interrupts();
      return false;
    }

    if (!wait_for_keyboard_irq_count(before_irq_count + i + 1,
                                     kKeyboardTestTimeoutTicks)) {
      disable_interrupts();
      return false;
    }
  }

  char line_buffer[kConsoleLineBufferCapacity];
  const size_t line_length =
      console_read_line(line_buffer, sizeof(line_buffer));

  disable_interrupts();

  serial_write_string("console_line_length=");
  serial_write_u64(line_length);
  serial_write_crlf();

  serial_write_string("console_line=");
  serial_write_string(line_buffer);
  serial_write_crlf();

  serial_write_string("console_buffer_remaining=");
  serial_write_u64(keyboard_buffered_char_count());
  serial_write_crlf();

  return keyboard_irq_count() == expected_irq_count &&
         line_length == string_length(kConsoleExpectedLine) &&
         strings_equal(line_buffer, kConsoleExpectedLine) &&
         keyboard_buffered_char_count() == 0;
}

struct ShellSmokeCommand {
  const char* expected_line;              // 这一条命令读取出来后，应该得到哪一行文本。
  const uint8_t* scancodes;               // 用哪些扫描码把这条命令注入进键盘路径。
  size_t scancode_count;                  // 这条命令一共注入多少个扫描码。
  ShellCommandResult expected_result;     // 这条命令执行后，应该得到什么分类结果。
};

constexpr ShellSmokeCommand kShellSmokeCommands[] = {
    {"help", kShellHelpScancodes,
     sizeof(kShellHelpScancodes) / sizeof(kShellHelpScancodes[0]),
     kShellCommandExecuted},
    {"mem", kShellMemScancodes,
     sizeof(kShellMemScancodes) / sizeof(kShellMemScancodes[0]),
     kShellCommandExecuted},
    {"ticks", kShellTicksScancodes,
     sizeof(kShellTicksScancodes) / sizeof(kShellTicksScancodes[0]),
     kShellCommandExecuted},
    {"heap", kShellHeapScancodes,
     sizeof(kShellHeapScancodes) / sizeof(kShellHeapScancodes[0]),
     kShellCommandExecuted},
    {"irq", kShellIrqScancodes,
     sizeof(kShellIrqScancodes) / sizeof(kShellIrqScancodes[0]),
     kShellCommandExecuted},
    {"bootinfo", kShellBootInfoScancodes,
     sizeof(kShellBootInfoScancodes) / sizeof(kShellBootInfoScancodes[0]),
     kShellCommandExecuted},
    {"e820", kShellE820Scancodes,
     sizeof(kShellE820Scancodes) / sizeof(kShellE820Scancodes[0]),
     kShellCommandExecuted},
    {"cpu", kShellCpuScancodes,
     sizeof(kShellCpuScancodes) / sizeof(kShellCpuScancodes[0]),
     kShellCommandExecuted},
    {"echo hi42", kShellEditedEchoScancodes,
     sizeof(kShellEditedEchoScancodes) / sizeof(kShellEditedEchoScancodes[0]),
     kShellCommandExecuted},
    {"uptime", kShellUptimeScancodes,
     sizeof(kShellUptimeScancodes) / sizeof(kShellUptimeScancodes[0]),
     kShellCommandExecuted},
    {"history", kShellHistoryScancodes,
     sizeof(kShellHistoryScancodes) / sizeof(kShellHistoryScancodes[0]),
     kShellCommandExecuted},
    {"history", kShellHistoryBrowseScancodes,
     sizeof(kShellHistoryBrowseScancodes) / sizeof(kShellHistoryBrowseScancodes[0]),
     kShellCommandExecuted},
    {"clear", kShellClearScancodes,
     sizeof(kShellClearScancodes) / sizeof(kShellClearScancodes[0]),
     kShellCommandExecuted},
    {"bad", kShellBadScancodes,
     sizeof(kShellBadScancodes) / sizeof(kShellBadScancodes[0]),
     kShellCommandUnknown},
};

bool inject_scancode_sequence(const char* log_prefix,
                              const uint8_t* scancodes,
                              size_t scancode_count,
                              uint64_t* next_expected_irq_count) {
  if (log_prefix == nullptr || scancodes == nullptr ||
      next_expected_irq_count == nullptr) {
    return false;
  }

  for (size_t i = 0; i < scancode_count; ++i) {
    serial_write_string(log_prefix);
    serial_write_char('[');
    serial_write_u64(i);
    serial_write_string("]=0x");
    serial_write_hex64(scancodes[i]);
    serial_write_crlf();

    if (!keyboard_inject_test_scancode(scancodes[i])) {
      return false;
    }

    ++(*next_expected_irq_count);
    if (!wait_for_keyboard_irq_count(*next_expected_irq_count,
                                     kKeyboardTestTimeoutTicks)) {
      return false;
    }
  }

  return true;
}

bool run_shell_smoke_test(const BootInfo* boot_info) {
  initialize_console(kShellTestStartRow, kShellTextColor);

  if (!initialize_shell(&g_shell, boot_info, &g_page_allocator, &g_kernel_heap,
                        &kShellOutput)) {
    return false;
  }

  ConsoleHistoryProvider history_provider;
  history_provider.entry_count = shell_history_provider_count;
  history_provider.entry_text = shell_history_provider_text;
  history_provider.context = &g_shell;

  uint64_t expected_irq_count = keyboard_irq_count();
  enable_interrupts();

  for (size_t command_index = 0;
       command_index < (sizeof(kShellSmokeCommands) / sizeof(kShellSmokeCommands[0]));
       ++command_index) {
    const ShellSmokeCommand& command = kShellSmokeCommands[command_index];

    shell_print_prompt(&g_shell);
    if (!inject_scancode_sequence("shell_inject",
                                  command.scancodes,
                                  command.scancode_count,
                                  &expected_irq_count)) {
      disable_interrupts();
      return false;
    }

    char line_buffer[kShellLineBufferCapacity];
    const size_t line_length =
        console_read_line_with_history(line_buffer, sizeof(line_buffer),
                                       &history_provider);

    serial_write_string("shell_line=");
    serial_write_string(line_buffer);
    serial_write_crlf();

    serial_write_string("shell_line_length=");
    serial_write_u64(line_length);
    serial_write_crlf();

    const ShellCommandResult result =
        shell_execute_line(&g_shell, line_buffer);

    serial_write_string("shell_result=");
    serial_write_string(shell_command_result_name(result));
    serial_write_crlf();

    if (!strings_equal(line_buffer, command.expected_line) ||
        line_length != string_length(command.expected_line) ||
        result != command.expected_result) {
      disable_interrupts();
      return false;
    }
  }

  disable_interrupts();
  return keyboard_irq_count() == expected_irq_count &&
         keyboard_buffered_char_count() == 0;
}

#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
void run_invalid_opcode_smoke_test() {
  serial_write_string("invalid_opcode_marker=0x");
  serial_write_hex64(kInvalidOpcodeMarker);
  serial_write_crlf();

  asm volatile("ud2");   // `ud2` 是 x86 专门留给“故意触发非法指令异常”的指令。
}
#endif

#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
void run_page_fault_smoke_test() {
  serial_write_string("page_fault_smoke_address=0x");
  serial_write_hex64(kPageFaultSmokeAddress);
  serial_write_crlf();

  auto* const fault_ptr = reinterpret_cast<volatile uint64_t*>(
      static_cast<uintptr_t>(kPageFaultSmokeAddress));
  *fault_ptr = kPageFaultSmokePattern;  // 这里应该触发 page fault，正常不会再往下走。
}
#endif

}  // namespace

extern "C" void kernel_main(const BootInfo* boot_info) {
  write_status_line(4, "hello from os64 kernel");

  if (!initialize_idt()) {
    write_status_line(5, "idt bad");
    return;
  }

  write_status_line(5, "idt ok");

  if (!is_boot_info_valid(boot_info)) {
    write_status_line(6, "boot info bad");
    return;
  }

  write_status_line(6, "boot info ok");

  log_e820_entries(boot_info);                 // 第一步：真正把 BIOS 给的内存地图读出来。
  write_status_line(7, "e820 parse ok");

  if (!initialize_page_allocator(&g_page_allocator, boot_info)) {
    write_status_line(8, "page allocator bad");
    return;
  }

  log_allocator_ranges(g_page_allocator);      // 第二步：把 usable 区域裁成真正可分配的页区间。
  log_allocated_pages(&g_page_allocator);      // 第三步：实际分几个页，证明 alloc_page() 已经能用了。

  serial_write_string("free_pages_remaining=");
  serial_write_u64(count_free_pages(&g_page_allocator));
  serial_write_crlf();

  write_status_line(8, "page allocator ok");

  if (!run_paging_smoke_test(&g_page_allocator)) {
    write_status_line(9, "map_page bad");
    return;
  }

  write_status_line(9, "map_page ok");

  if (!initialize_kernel_heap(&g_kernel_heap, &g_page_allocator)) {
    write_status_line(10, "heap init bad");
    return;
  }

  write_status_line(10, "heap init ok");

  if (!run_heap_smoke_test(&g_kernel_heap)) {
    write_status_line(11, "heap alloc bad");
    return;
  }

  write_status_line(11, "heap alloc ok");

  if (!run_timer_smoke_test()) {
    write_status_line(12, "timer bad");
    return;
  }

  write_status_line(12, "timer ok");

  if (!run_keyboard_smoke_test()) {
    write_status_line(13, "keyboard bad");
    return;
  }

  write_status_line(13, "keyboard ok");

  if (!run_console_input_smoke_test()) {
    write_status_line(14, "console input bad");
    return;
  }

  write_status_line(14, "console input ok");

  if (!run_shell_smoke_test(boot_info)) {
    write_status_line(15, "shell bad");
    return;
  }

  write_status_line(15, "shell ok");

#if defined(OS64_ENABLE_PAGE_FAULT_SMOKE)
  write_status_line(16, "page fault smoke");
  run_page_fault_smoke_test();
#endif

#if defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
  write_status_line(16, "invalid opcode smoke");
  run_invalid_opcode_smoke_test();
#endif

#if !defined(OS64_ENABLE_PAGE_FAULT_SMOKE) && !defined(OS64_ENABLE_INVALID_OPCODE_SMOKE)
  enable_interrupts();  // 真正进入交互 shell 前重新开中断，不然 `hlt` 等键盘时不会再醒。
  char shell_line_buffer[kShellLineBufferCapacity];
  shell_run_forever(&g_shell, shell_line_buffer, sizeof(shell_line_buffer));
#endif
}

extern "C" void kernel_handle_exception(const InterruptFrame* frame,
                                         uint64_t fault_address) {
  vga_write_line(12, "kernel exception", kExceptionTextColor);

  serial_write_string("exception=");
  serial_write_string(exception_name(frame->vector));
  serial_write_crlf();

  serial_write_string("vector=0x");
  serial_write_hex64(frame->vector);
  serial_write_crlf();

  serial_write_string("error_code=0x");
  serial_write_hex64(frame->error_code);
  serial_write_crlf();

  if (frame->vector == 14) {
    serial_write_string("fault_address=0x");
    serial_write_hex64(fault_address);
    serial_write_crlf();
  }

  serial_write_string("fault_rip=0x");
  serial_write_hex64(frame->rip);
  serial_write_crlf();
}
