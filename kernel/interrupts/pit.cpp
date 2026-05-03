#include "interrupts/pit.hpp"

#include "interrupts/interrupts.hpp"
#include "task/scheduler.hpp"

namespace {

constexpr uint16_t kPitChannel0DataPort = 0x40;    // PIT 通道 0 数据端口，连到 IRQ0。
constexpr uint16_t kPitCommandPort = 0x43;         // PIT 模式/通道选择命令端口。
constexpr uint32_t kPitInputFrequency = 1193182;   // 经典 PIT 的输入时钟，大约 1.193182 MHz。
constexpr uint8_t kPitModeRateGenerator = 0x34;    // 通道 0，低高字节，模式 2，二进制计数。
constexpr uint64_t kMillisecondsPerSecond = 1000;  // 1 秒 = 1000 毫秒，给 `sleep_ms` 做单位换算。

volatile uint64_t g_timer_ticks = 0;               // 这就是第一版“系统时间心跳”的最小计数器。
uint32_t g_timer_frequency_hz = 0;                 // 记录 PIT 现在的频率，后面换算毫秒要用。
bool g_timer_ready = false;                        // 让外部知道 PIT 是否已经真的配好。

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
  g_timer_frequency_hz = frequency_hz;
  g_timer_ready = false;

  // 先写命令字，告诉 PIT：
  // 1. 用通道 0
  // 2. 分两次写入低字节和高字节
  // 3. 使用 rate generator 模式
  out8(kPitCommandPort, kPitModeRateGenerator);

  // 再把 divisor 的低 8 位和高 8 位送进去。
  out8(kPitChannel0DataPort, static_cast<uint8_t>(divisor & 0xFF));
  out8(kPitChannel0DataPort, static_cast<uint8_t>((divisor >> 8) & 0xFF));
  g_timer_ready = true;
  return true;
}

void handle_timer_irq() {
  // 第一版先只做一件最重要的事：
  // 来一次时钟中断，就把 tick 加 1。
  ++g_timer_ticks;

  // 现在 timer IRQ 不只是在数“时间过去了多少”，
  // 也开始给当前线程记账，并在时间片耗尽时发出 reschedule 请求。
  scheduler_handle_timer_tick();
}

uint64_t timer_tick_count() {
  return g_timer_ticks;
}

uint32_t timer_frequency_hz() {
  return g_timer_frequency_hz;
}

bool timer_is_ready() {
  return g_timer_ready;
}

void timer_wait_ticks(uint64_t ticks) {
  if (ticks == 0 || !g_timer_ready) {
    return;
  }

  // 先记住“从第几个 tick 开始等”，
  // 后面只要差值还没到目标，就继续睡到下一次中断。
  const uint64_t start_tick = g_timer_ticks;
  while ((g_timer_ticks - start_tick) < ticks) {
    wait_for_interrupt();

    // 当前版本还不在 IRQ 里直接做抢占式切栈，
    // 所以先在这种从 `hlt` 醒来的安全点响应“该换人了”的请求。
    (void)scheduler_yield_if_requested();
  }
}

bool timer_sleep_ms(uint64_t milliseconds) {
  if (!g_timer_ready) {
    return false;
  }

  if (milliseconds == 0) {
    return true;
  }

  // 先把“毫秒”换成“至少多少个 tick”。
  // 用向上取整，是为了避免比如 1ms 在低频时被算成 0 tick，导致完全不等。
  uint64_t ticks =
      (milliseconds * static_cast<uint64_t>(g_timer_frequency_hz) +
       (kMillisecondsPerSecond - 1)) /
      kMillisecondsPerSecond;
  if (ticks == 0) {
    ticks = 1;
  }

  timer_wait_ticks(ticks);
  return true;
}
