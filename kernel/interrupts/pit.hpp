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

#endif
