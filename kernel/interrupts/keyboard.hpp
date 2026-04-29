#ifndef OS64_KEYBOARD_HPP
#define OS64_KEYBOARD_HPP

#include <stdbool.h>
#include <stdint.h>

enum KeyboardInputKind : uint8_t {
  kKeyboardInputCharacter = 0,  // 普通可见字符、回车、退格这类“字符型输入”。
  kKeyboardInputArrowUp = 1,
  kKeyboardInputArrowDown = 2,
  kKeyboardInputArrowLeft = 3,
  kKeyboardInputArrowRight = 4,
  kKeyboardInputDelete = 5,
  kKeyboardInputHome = 6,
  kKeyboardInputEnd = 7,
};

struct KeyboardInputEvent {
  KeyboardInputKind kind;
  char character;  // 只有 `kKeyboardInputCharacter` 时才真的使用它。
};

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

// 当前字符缓冲区里还攒着多少个“已经翻译好的字符”。
// 这里的字符不是原始扫描码，而是像 'a'、'1'、'\n'、'\b' 这样的可消费输入。
uint16_t keyboard_buffered_char_count();

// 如果环形缓冲区满了，后续字符会被丢掉。
// 这个计数器能让测试先看出“有没有因为缓冲区太小而悄悄丢输入”。
uint64_t keyboard_dropped_char_count();

// 尝试取出 1 个完整键盘输入事件。
// 这一轮开始，方向键 / Home / End / Delete 也会走这条路径。
bool keyboard_try_read_input_event(KeyboardInputEvent* out_event);

// 尝试从键盘字符缓冲区里拿出 1 个字符。
// 成功时返回 true，并把字符写进 out_char；没有字符时返回 false。
// 它现在主要给旧测试和只关心字符的调用方兼容使用。
bool keyboard_try_read_char(char* out_char);

// 给当前测试环境注入一个“像是键盘刚发来的扫描码”。
// 这一轮主要给 QEMU 里的自动测试自举使用。
bool keyboard_inject_test_scancode(uint8_t scancode);

#endif
