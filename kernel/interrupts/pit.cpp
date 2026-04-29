#include "interrupts/pit.hpp"

namespace {

constexpr uint16_t kPitChannel0DataPort = 0x40;    // PIT 通道 0 数据端口，连到 IRQ0。
constexpr uint16_t kPitCommandPort = 0x43;         // PIT 模式/通道选择命令端口。
constexpr uint32_t kPitInputFrequency = 1193182;   // 经典 PIT 的输入时钟，大约 1.193182 MHz。
constexpr uint8_t kPitModeRateGenerator = 0x34;    // 通道 0，低高字节，模式 2，二进制计数。

volatile uint64_t g_timer_ticks = 0;               // 这就是第一版“系统时间心跳”的最小计数器。

inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

}  // namespace

bool initialize_pit(uint32_t frequency_hz) {
  if (frequency_hz == 0) {
    return false;
  }

  // PIT 的工作方式是：
  // 输入时钟固定约 1.193182 MHz，
  // 我们给它一个 divisor，它每数满一次就产生一个 tick。
  uint32_t divisor = kPitInputFrequency / frequency_hz;
  if (divisor == 0 || divisor > 0xFFFF) {
    return false;
  }

  // 每次重新初始化 PIT，都把“已经过去多少 tick”从 0 开始算。
  g_timer_ticks = 0;

  // 先写命令字，告诉 PIT：
  // 1. 用通道 0
  // 2. 分两次写入低字节和高字节
  // 3. 使用 rate generator 模式
  out8(kPitCommandPort, kPitModeRateGenerator);

  // 再把 divisor 的低 8 位和高 8 位送进去。
  out8(kPitChannel0DataPort, static_cast<uint8_t>(divisor & 0xFF));
  out8(kPitChannel0DataPort, static_cast<uint8_t>((divisor >> 8) & 0xFF));
  return true;
}

void handle_timer_irq() {
  // 第一版先只做一件最重要的事：
  // 来一次时钟中断，就把 tick 加 1。
  ++g_timer_ticks;
}

uint64_t timer_tick_count() {
  return g_timer_ticks;
}
