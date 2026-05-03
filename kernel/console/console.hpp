#ifndef OS64_CONSOLE_HPP
#define OS64_CONSOLE_HPP

#include <stddef.h>
#include <stdint.h>

// 控制台本身不理解 shell，只理解“如果用户按了上下键，我要不要能拿到历史文本”。
// 所以这里单独抽象一个最小 history provider，让 console 继续保持通用。
struct ConsoleHistoryProvider {
  size_t (*entry_count)(const void* context);                 // 当前一共能回看的历史条数。
  const char* (*entry_text)(const void* context, size_t index);  // 按从旧到新的顺序返回第 index 条历史。
  const void* context;
};

// 初始化最小 VGA 控制台。
// 这里的 start_row 不是“屏幕从哪开始显示 BIOS”，
// 而是告诉控制台：前面这些行可能已经被状态日志占用，你从哪一行开始作为自己的输入/输出区域。
void initialize_console(uint16_t start_row, uint8_t color);

// 配置控制台真正可写的列范围，右边界是开区间。
// 例如 [2, 78) 表示左右各留 2 列空白，这样文字不会直接贴到屏幕边缘。
void console_set_viewport(uint16_t start_column, uint16_t end_column);

// 有些更早期的子系统会在 console 正式初始化前就起来。
// 这个查询接口让它们能决定：现在要不要真的往 VGA 控制台写。
bool console_is_initialized();

// 往最小控制台写 1 个字符。
// 这一轮只支持：
// - 普通可打印字符
// - '\n' 换行
// - '\b' 退格
void console_write_char(char ch);

// 连续写一个 0 结尾字符串。
void console_write_string(const char* text);

// 修改后续 VGA 文本输出使用的前景/背景颜色。
// 这一轮主要给 shell 提示符做一个更轻一点的配色。
void console_set_color(uint8_t color);

// 清空控制台自己的显示区域，并把光标重置回起始位置。
// 这一轮主要给 `clear` 命令使用。
void console_clear();

// 阻塞读取一整行字符，并直接在 VGA 上回显用户输入。
// 读取规则：
// - 普通字符：加入缓冲区并回显
// - '\b'：删除行缓冲区最后 1 个字符，并在屏幕上退格
// - '\n'：结束当前行，补 '\0' 后返回
//
// 返回值是这一行真正读到了多少个可见字符，不包含结尾 '\0'。
// capacity 至少应 >= 2，这样才能容纳“1 个字符 + 结尾 0”。
size_t console_read_line(char* buffer, size_t capacity);

// 带 history 浏览能力的版本。
// 这一轮开始支持：
// - 左右方向键
// - Home / End
// - Delete
// - 上下方向键浏览历史
size_t console_read_line_with_history(char* buffer,
                                      size_t capacity,
                                      const ConsoleHistoryProvider* history);

#endif
