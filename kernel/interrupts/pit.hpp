#ifndef OS64_PIT_HPP
#define OS64_PIT_HPP

#include <stdbool.h>
#include <stdint.h>

// 初始化经典 PIT 定时器。
// 传入的是你希望每秒产生多少次 IRQ0，比如 100Hz。
// 简单理解：频率越高，时钟 tick 越密，sleep/yield 的颗粒度也越细。
bool initialize_pit(uint32_t frequency_hz);

// 每次收到 IRQ0 时，由中断路径调用它，把全局 tick 计数加 1。
void handle_timer_irq();

// 读当前已经发生了多少次时钟 tick。
// 这就是第一版最小“系统时间节拍”。
uint64_t timer_tick_count();

// 读当前 PIT 被配置成了多少 Hz。
// 比如返回 100，就表示“每秒大约产生 100 次 tick”。
uint32_t timer_frequency_hz();

// 判断 PIT 是否已经初始化完成。
bool timer_is_ready();

// 正式线程登记 Sleeping，到 BSP 全局 tick 期限后被唤醒，等待期间交还 CPU/锁。
// 无线程的早期启动才用 HLT；这条启动路径要求中断已经打开。
void timer_wait_ticks(uint64_t ticks);

// 按“毫秒”这个更好理解的单位等待。
// 活动线程直接使用调度睡眠；启动路径使用 timer_wait_ticks。
// 未初始化或换算/期限溢出返回 false。
bool timer_sleep_ms(uint64_t milliseconds);

#endif
