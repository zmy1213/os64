#include "interrupts/keyboard.hpp"

namespace {

constexpr uint16_t kKeyboardDataPort = 0x60;          // 键盘数据端口，扫描码最终从这里读出来。
constexpr uint16_t kKeyboardStatusPort = 0x64;        // 键盘控制器状态/命令端口。
constexpr uint8_t kStatusOutputBufferFull = 0x01;     // bit0=1 表示已经有字节可读。
constexpr uint8_t kStatusInputBufferFull = 0x02;      // bit1=1 表示控制器还忙，暂时别再写命令。
constexpr uint8_t kInjectOutputBufferCommand = 0xD2;  // 8042 命令：把下一个字节塞进输出缓冲，模拟键盘送数据。
constexpr uint32_t kControllerPollLimit = 100000;     // 防止控制器异常时无限卡在轮询里。
constexpr uint16_t kKeyboardCharBufferCapacity = 64;  // 第一版先留 64 个字符槽位，够演示连续输入和 FIFO 顺序。

volatile uint64_t g_keyboard_irq_count = 0;           // 已经处理过多少次 IRQ1。
volatile uint8_t g_keyboard_last_scancode = 0;        // 最近一次读到的扫描码。
volatile uint8_t g_keyboard_char_buffer[kKeyboardCharBufferCapacity];
volatile uint16_t g_keyboard_char_read_index = 0;     // 下一个要被消费者取走的字符位置。
volatile uint16_t g_keyboard_char_write_index = 0;    // 下一个新字符应该写入的位置。
volatile uint16_t g_keyboard_char_count = 0;          // 当前缓冲区里已经排队了多少个字符。
volatile uint64_t g_keyboard_dropped_char_count = 0;  // 如果缓冲区满了，被丢掉的字符数量记在这里。

inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t in8(uint16_t port) {
  uint8_t value = 0;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

uint64_t save_interrupt_flags_and_disable() {
  uint64_t flags = 0;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  return flags;
}

void restore_interrupt_flags(uint64_t flags) {
  asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

uint16_t advance_buffer_index(uint16_t index) {
  ++index;
  if (index == kKeyboardCharBufferCapacity) {
    return 0;
  }

  return index;
}

bool wait_for_input_buffer_empty() {
  for (uint32_t i = 0; i < kControllerPollLimit; ++i) {
    if ((in8(kKeyboardStatusPort) & kStatusInputBufferFull) == 0) {
      return true;
    }
  }

  return false;
}

void drain_output_buffer() {
  // 如果 BIOS 或更早阶段已经往控制器里留了旧字节，
  // 这里先把它们读掉，避免后面的测试把“陈年数据”当成新键。
  for (uint32_t i = 0; i < kControllerPollLimit; ++i) {
    if ((in8(kKeyboardStatusPort) & kStatusOutputBufferFull) == 0) {
      return;
    }

    (void)in8(kKeyboardDataPort);
  }
}

bool translate_scancode_to_char(uint8_t scancode, char* out_char) {
  if (out_char == nullptr) {
    return false;
  }

  // Set 1 里最高位为 1 的大多数扫描码都表示“按键松开”，
  // 这一轮先只关心“按下时产生哪个字符”，所以先把它们忽略掉。
  if ((scancode & 0x80) != 0) {
    return false;
  }

  switch (scancode) {
    case 0x02: *out_char = '1'; return true;
    case 0x03: *out_char = '2'; return true;
    case 0x04: *out_char = '3'; return true;
    case 0x05: *out_char = '4'; return true;
    case 0x06: *out_char = '5'; return true;
    case 0x07: *out_char = '6'; return true;
    case 0x08: *out_char = '7'; return true;
    case 0x09: *out_char = '8'; return true;
    case 0x0A: *out_char = '9'; return true;
    case 0x0B: *out_char = '0'; return true;

    case 0x10: *out_char = 'q'; return true;
    case 0x11: *out_char = 'w'; return true;
    case 0x12: *out_char = 'e'; return true;
    case 0x13: *out_char = 'r'; return true;
    case 0x14: *out_char = 't'; return true;
    case 0x15: *out_char = 'y'; return true;
    case 0x16: *out_char = 'u'; return true;
    case 0x17: *out_char = 'i'; return true;
    case 0x18: *out_char = 'o'; return true;
    case 0x19: *out_char = 'p'; return true;

    case 0x1E: *out_char = 'a'; return true;
    case 0x1F: *out_char = 's'; return true;
    case 0x20: *out_char = 'd'; return true;
    case 0x21: *out_char = 'f'; return true;
    case 0x22: *out_char = 'g'; return true;
    case 0x23: *out_char = 'h'; return true;
    case 0x24: *out_char = 'j'; return true;
    case 0x25: *out_char = 'k'; return true;
    case 0x26: *out_char = 'l'; return true;

    case 0x2C: *out_char = 'z'; return true;
    case 0x2D: *out_char = 'x'; return true;
    case 0x2E: *out_char = 'c'; return true;
    case 0x2F: *out_char = 'v'; return true;
    case 0x30: *out_char = 'b'; return true;
    case 0x31: *out_char = 'n'; return true;
    case 0x32: *out_char = 'm'; return true;

    case 0x39: *out_char = ' '; return true;
    case 0x1C: *out_char = '\n'; return true;
    case 0x0E: *out_char = '\b'; return true;
    default:
      return false;
  }
}

void enqueue_translated_char(uint8_t character) {
  if (g_keyboard_char_count >= kKeyboardCharBufferCapacity) {
    ++g_keyboard_dropped_char_count;
    return;
  }

  g_keyboard_char_buffer[g_keyboard_char_write_index] = character;
  g_keyboard_char_write_index =
      advance_buffer_index(g_keyboard_char_write_index);
  ++g_keyboard_char_count;
}

}  // namespace

bool initialize_keyboard() {
  drain_output_buffer();
  g_keyboard_irq_count = 0;
  g_keyboard_last_scancode = 0;
  g_keyboard_char_read_index = 0;
  g_keyboard_char_write_index = 0;
  g_keyboard_char_count = 0;
  g_keyboard_dropped_char_count = 0;
  return true;
}

void handle_keyboard_irq() {
  // 正常来说 IRQ1 到来时，输出缓冲区里就已经有 1 个扫描码了。
  // 这一层先只做最小事：把它读出来并记住。
  if ((in8(kKeyboardStatusPort) & kStatusOutputBufferFull) == 0) {
    return;
  }

  g_keyboard_last_scancode = in8(kKeyboardDataPort);
  ++g_keyboard_irq_count;

  char translated = '\0';
  if (translate_scancode_to_char(g_keyboard_last_scancode, &translated)) {
    enqueue_translated_char(static_cast<uint8_t>(translated));
  }
}

uint64_t keyboard_irq_count() {
  return g_keyboard_irq_count;
}

uint8_t keyboard_last_scancode() {
  return g_keyboard_last_scancode;
}

uint16_t keyboard_buffered_char_count() {
  return g_keyboard_char_count;
}

uint64_t keyboard_dropped_char_count() {
  return g_keyboard_dropped_char_count;
}

bool keyboard_try_read_char(char* out_char) {
  if (out_char == nullptr) {
    return false;
  }

  const uint64_t flags = save_interrupt_flags_and_disable();
  if (g_keyboard_char_count == 0) {
    restore_interrupt_flags(flags);
    return false;
  }

  const uint8_t buffered_char =
      g_keyboard_char_buffer[g_keyboard_char_read_index];
  g_keyboard_char_read_index =
      advance_buffer_index(g_keyboard_char_read_index);
  --g_keyboard_char_count;
  restore_interrupt_flags(flags);

  *out_char = static_cast<char>(buffered_char);
  return true;
}

bool keyboard_inject_test_scancode(uint8_t scancode) {
  // 这一轮不是要求用户手工按键，
  // 而是借助 8042 控制器的测试命令，在 QEMU 里人工塞 1 个扫描码进去。
  if (!wait_for_input_buffer_empty()) {
    return false;
  }

  out8(kKeyboardStatusPort, kInjectOutputBufferCommand);
  if (!wait_for_input_buffer_empty()) {
    return false;
  }

  out8(kKeyboardDataPort, scancode);
  return true;
}
