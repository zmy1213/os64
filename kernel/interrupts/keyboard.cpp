#include "interrupts/keyboard.hpp"

namespace {

constexpr uint16_t kKeyboardDataPort = 0x60;          // 键盘数据端口，扫描码最终从这里读出来。
constexpr uint16_t kKeyboardStatusPort = 0x64;        // 键盘控制器状态/命令端口。
constexpr uint8_t kStatusOutputBufferFull = 0x01;     // bit0=1 表示已经有字节可读。
constexpr uint8_t kStatusInputBufferFull = 0x02;      // bit1=1 表示控制器还忙，暂时别再写命令。
constexpr uint8_t kInjectOutputBufferCommand = 0xD2;  // 8042 命令：把下一个字节塞进输出缓冲，模拟键盘送数据。
constexpr uint32_t kControllerPollLimit = 100000;     // 防止控制器异常时无限卡在轮询里。
constexpr uint16_t kKeyboardInputBufferCapacity = 64; // 第一版先留 64 个输入事件槽位，够演示 FIFO 顺序和简单编辑。

volatile uint64_t g_keyboard_irq_count = 0;           // 已经处理过多少次 IRQ1。
volatile uint8_t g_keyboard_last_scancode = 0;        // 最近一次读到的扫描码。
volatile KeyboardInputEvent g_keyboard_input_buffer[kKeyboardInputBufferCapacity];
volatile uint16_t g_keyboard_input_read_index = 0;    // 下一个要被消费者取走的输入事件位置。
volatile uint16_t g_keyboard_input_write_index = 0;   // 下一个新输入事件应该写入的位置。
volatile uint16_t g_keyboard_input_count = 0;         // 当前缓冲区里一共排队了多少个输入事件。
volatile uint16_t g_keyboard_char_count = 0;          // 当前排队事件里，真正“字符型输入”有多少个。
volatile uint64_t g_keyboard_dropped_char_count = 0;  // 如果缓冲区满了，被丢掉的字符数量记在这里。
volatile uint8_t g_keyboard_has_extended_prefix = 0;  // `0xE0` 前缀表示接下来这个字节要按扩展键来解释。

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
  if (index == kKeyboardInputBufferCapacity) {
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

bool translate_regular_scancode(uint8_t scancode, KeyboardInputEvent* out_event) {
  if (out_event == nullptr) {
    return false;
  }

  // Set 1 里最高位为 1 的大多数扫描码都表示“按键松开”，
  // 这一轮先只关心“按下时产生哪个字符”，所以先把它们忽略掉。
  if ((scancode & 0x80) != 0) {
    return false;
  }

  out_event->kind = kKeyboardInputCharacter;

  switch (scancode) {
    case 0x02: out_event->character = '1'; return true;
    case 0x03: out_event->character = '2'; return true;
    case 0x04: out_event->character = '3'; return true;
    case 0x05: out_event->character = '4'; return true;
    case 0x06: out_event->character = '5'; return true;
    case 0x07: out_event->character = '6'; return true;
    case 0x08: out_event->character = '7'; return true;
    case 0x09: out_event->character = '8'; return true;
    case 0x0A: out_event->character = '9'; return true;
    case 0x0B: out_event->character = '0'; return true;

    case 0x10: out_event->character = 'q'; return true;
    case 0x11: out_event->character = 'w'; return true;
    case 0x12: out_event->character = 'e'; return true;
    case 0x13: out_event->character = 'r'; return true;
    case 0x14: out_event->character = 't'; return true;
    case 0x15: out_event->character = 'y'; return true;
    case 0x16: out_event->character = 'u'; return true;
    case 0x17: out_event->character = 'i'; return true;
    case 0x18: out_event->character = 'o'; return true;
    case 0x19: out_event->character = 'p'; return true;

    case 0x1E: out_event->character = 'a'; return true;
    case 0x1F: out_event->character = 's'; return true;
    case 0x20: out_event->character = 'd'; return true;
    case 0x21: out_event->character = 'f'; return true;
    case 0x22: out_event->character = 'g'; return true;
    case 0x23: out_event->character = 'h'; return true;
    case 0x24: out_event->character = 'j'; return true;
    case 0x25: out_event->character = 'k'; return true;
    case 0x26: out_event->character = 'l'; return true;

    case 0x2C: out_event->character = 'z'; return true;
    case 0x2D: out_event->character = 'x'; return true;
    case 0x2E: out_event->character = 'c'; return true;
    case 0x2F: out_event->character = 'v'; return true;
    case 0x30: out_event->character = 'b'; return true;
    case 0x31: out_event->character = 'n'; return true;
    case 0x32: out_event->character = 'm'; return true;
    case 0x34: out_event->character = '.'; return true;
    case 0x35: out_event->character = '/'; return true;

    case 0x39: out_event->character = ' '; return true;
    case 0x1C: out_event->character = '\n'; return true;
    case 0x0E: out_event->character = '\b'; return true;
    default:
      return false;
  }
}

bool translate_extended_scancode(uint8_t scancode, KeyboardInputEvent* out_event) {
  if (out_event == nullptr) {
    return false;
  }

  // 扩展键也同样会有“按下/松开”之分，带最高位的松开事件这一轮先继续忽略。
  if ((scancode & 0x80) != 0) {
    return false;
  }

  out_event->character = '\0';

  switch (scancode) {
    case 0x48: out_event->kind = kKeyboardInputArrowUp; return true;
    case 0x50: out_event->kind = kKeyboardInputArrowDown; return true;
    case 0x4B: out_event->kind = kKeyboardInputArrowLeft; return true;
    case 0x4D: out_event->kind = kKeyboardInputArrowRight; return true;
    case 0x53: out_event->kind = kKeyboardInputDelete; return true;
    case 0x47: out_event->kind = kKeyboardInputHome; return true;
    case 0x4F: out_event->kind = kKeyboardInputEnd; return true;
    default:
      return false;
  }
}

bool translate_scancode_to_input_event(uint8_t scancode,
                                       KeyboardInputEvent* out_event) {
  if (scancode == 0xE0) {
    g_keyboard_has_extended_prefix = 1;
    return false;
  }

  if (g_keyboard_has_extended_prefix != 0) {
    g_keyboard_has_extended_prefix = 0;
    return translate_extended_scancode(scancode, out_event);
  }

  return translate_regular_scancode(scancode, out_event);
}

void enqueue_input_event(const KeyboardInputEvent& event) {
  if (g_keyboard_input_count >= kKeyboardInputBufferCapacity) {
    ++g_keyboard_dropped_char_count;
    return;
  }

  g_keyboard_input_buffer[g_keyboard_input_write_index].kind = event.kind;
  g_keyboard_input_buffer[g_keyboard_input_write_index].character =
      event.character;
  g_keyboard_input_write_index =
      advance_buffer_index(g_keyboard_input_write_index);
  ++g_keyboard_input_count;

  if (event.kind == kKeyboardInputCharacter) {
    ++g_keyboard_char_count;
  }
}

bool try_dequeue_input_event(KeyboardInputEvent* out_event,
                             bool characters_only) {
  if (out_event == nullptr) {
    return false;
  }

  if (g_keyboard_input_count == 0) {
    return false;
  }

  KeyboardInputEvent buffered_event;
  buffered_event.kind =
      g_keyboard_input_buffer[g_keyboard_input_read_index].kind;
  buffered_event.character =
      g_keyboard_input_buffer[g_keyboard_input_read_index].character;

  if (characters_only && buffered_event.kind != kKeyboardInputCharacter) {
    return false;
  }

  g_keyboard_input_read_index =
      advance_buffer_index(g_keyboard_input_read_index);
  --g_keyboard_input_count;

  if (buffered_event.kind == kKeyboardInputCharacter) {
    --g_keyboard_char_count;
  }

  *out_event = buffered_event;
  return true;
}

}  // namespace

bool initialize_keyboard() {
  drain_output_buffer();
  g_keyboard_irq_count = 0;
  g_keyboard_last_scancode = 0;
  g_keyboard_input_read_index = 0;
  g_keyboard_input_write_index = 0;
  g_keyboard_input_count = 0;
  g_keyboard_char_count = 0;
  g_keyboard_dropped_char_count = 0;
  g_keyboard_has_extended_prefix = 0;
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

  KeyboardInputEvent translated_event;
  translated_event.kind = kKeyboardInputCharacter;
  translated_event.character = '\0';
  if (translate_scancode_to_input_event(g_keyboard_last_scancode,
                                        &translated_event)) {
    enqueue_input_event(translated_event);
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

bool keyboard_try_read_input_event(KeyboardInputEvent* out_event) {
  if (out_event == nullptr) {
    return false;
  }

  const uint64_t flags = save_interrupt_flags_and_disable();
  const bool success = try_dequeue_input_event(out_event, false);
  restore_interrupt_flags(flags);
  return success;
}

bool keyboard_try_read_char(char* out_char) {
  if (out_char == nullptr) {
    return false;
  }

  KeyboardInputEvent event;
  event.kind = kKeyboardInputCharacter;
  event.character = '\0';
  const uint64_t flags = save_interrupt_flags_and_disable();
  const bool success = try_dequeue_input_event(&event, true);
  restore_interrupt_flags(flags);
  if (!success) {
    return false;
  }

  *out_char = event.character;
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
