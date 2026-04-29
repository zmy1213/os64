#include "interrupts/keyboard.hpp"

namespace {

constexpr uint16_t kKeyboardDataPort = 0x60;          // 键盘数据端口，扫描码最终从这里读出来。
constexpr uint16_t kKeyboardStatusPort = 0x64;        // 键盘控制器状态/命令端口。
constexpr uint8_t kStatusOutputBufferFull = 0x01;     // bit0=1 表示已经有字节可读。
constexpr uint8_t kStatusInputBufferFull = 0x02;      // bit1=1 表示控制器还忙，暂时别再写命令。
constexpr uint8_t kInjectOutputBufferCommand = 0xD2;  // 8042 命令：把下一个字节塞进输出缓冲，模拟键盘送数据。
constexpr uint32_t kControllerPollLimit = 100000;     // 防止控制器异常时无限卡在轮询里。

volatile uint64_t g_keyboard_irq_count = 0;           // 已经处理过多少次 IRQ1。
volatile uint8_t g_keyboard_last_scancode = 0;        // 最近一次读到的扫描码。

inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t in8(uint16_t port) {
  uint8_t value = 0;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
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

}  // namespace

bool initialize_keyboard() {
  drain_output_buffer();
  g_keyboard_irq_count = 0;
  g_keyboard_last_scancode = 0;
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
}

uint64_t keyboard_irq_count() {
  return g_keyboard_irq_count;
}

uint8_t keyboard_last_scancode() {
  return g_keyboard_last_scancode;
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
