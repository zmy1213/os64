#ifndef OS64_KEYBOARD_HPP
#define OS64_KEYBOARD_HPP

#include <stdbool.h>
#include <stdint.h>

// 初始化最小键盘状态。
// 这一轮先不做完整键盘驱动，只清理旧数据、清零计数器，方便后面做 IRQ1 测试。
bool initialize_keyboard();

// 当 IRQ1 到来时，由中断路径调用它。
// 它会从键盘控制器里读出 1 个扫描码并记下来。
void handle_keyboard_irq();

// 一共收到了多少次键盘 IRQ。
uint64_t keyboard_irq_count();

// 最近一次收到的扫描码。
uint8_t keyboard_last_scancode();

// 给当前测试环境注入一个“像是键盘刚发来的扫描码”。
// 这一轮主要给 QEMU 里的自动测试自举使用。
bool keyboard_inject_test_scancode(uint8_t scancode);

#endif
