#ifndef OS64_CONSOLE_HPP
#define OS64_CONSOLE_HPP

#include <stddef.h>
#include <stdint.h>

// 初始化最小 VGA 控制台。
// 这里的 start_row 不是“屏幕从哪开始显示 BIOS”，
// 而是告诉控制台：前面这些行可能已经被状态日志占用，你从哪一行开始作为自己的输入/输出区域。
void initialize_console(uint16_t start_row, uint8_t color);

// 往最小控制台写 1 个字符。
// 这一轮只支持：
// - 普通可打印字符
// - '\n' 换行
// - '\b' 退格
void console_write_char(char ch);

// 连续写一个 0 结尾字符串。
void console_write_string(const char* text);

// 阻塞读取一整行字符，并直接在 VGA 上回显用户输入。
// 读取规则：
// - 普通字符：加入缓冲区并回显
// - '\b'：删除行缓冲区最后 1 个字符，并在屏幕上退格
// - '\n'：结束当前行，补 '\0' 后返回
//
// 返回值是这一行真正读到了多少个可见字符，不包含结尾 '\0'。
// capacity 至少应 >= 2，这样才能容纳“1 个字符 + 结尾 0”。
size_t console_read_line(char* buffer, size_t capacity);

#endif
