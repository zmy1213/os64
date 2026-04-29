#ifndef OS64_PIT_HPP
#define OS64_PIT_HPP

#include <stdbool.h>
#include <stdint.h>

// 初始化经典 PIT 定时器。
// 传入的是你希望每秒产生多少次 IRQ0，比如 100Hz。
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

// 至少等待这么多个 tick。
// 注意：调用它之前，中断必须已经打开，不然 `hlt` 之后就不会再醒。
void timer_wait_ticks(uint64_t ticks);

// 按“毫秒”这个更好理解的单位等待。
// 内部会先把毫秒换算成 tick，再复用 `timer_wait_ticks()`。
bool timer_sleep_ms(uint64_t milliseconds);

#endif
