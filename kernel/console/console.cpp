#include "console/console.hpp"

#include "interrupts/interrupts.hpp"
#include "interrupts/keyboard.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;    // VGA 文本模式固定每行 80 列。
constexpr uint16_t kVgaRows = 25;       // VGA 文本模式固定 25 行。
constexpr uintptr_t kVgaBase = 0xB8000; // VGA 文本缓冲区物理地址。

uint16_t g_console_start_row = 0;       // 控制台自己的显示区域从哪一行开始。
uint16_t g_console_row = 0;             // 当前光标所在行。
uint16_t g_console_column = 0;          // 当前光标所在列。
uint8_t g_console_color = 0x0F;         // 当前字符颜色，默认白字黑底。

volatile uint16_t* vga_buffer() {
  return reinterpret_cast<volatile uint16_t*>(kVgaBase);
}

uint16_t vga_cell(char ch, uint8_t color) {
  return static_cast<uint16_t>(color) << 8 |
         static_cast<uint8_t>(ch);
}

void clear_row(uint16_t row) {
  volatile uint16_t* const vga = vga_buffer();

  for (uint16_t column = 0; column < kVgaColumns; ++column) {
    vga[row * kVgaColumns + column] = vga_cell(' ', g_console_color);
  }
}

void scroll_if_needed() {
  if (g_console_row < kVgaRows) {
    return;
  }

  volatile uint16_t* const vga = vga_buffer();

  // 如果控制台已经写到屏幕底部，就把自己的显示区域整体上卷一行。
  // 这样前面的状态行还能保留，而新的输入/输出还能继续往下滚。
  for (uint16_t row = g_console_start_row; row + 1 < kVgaRows; ++row) {
    for (uint16_t column = 0; column < kVgaColumns; ++column) {
      vga[row * kVgaColumns + column] =
          vga[(row + 1) * kVgaColumns + column];
    }
  }

  clear_row(static_cast<uint16_t>(kVgaRows - 1));
  g_console_row = static_cast<uint16_t>(kVgaRows - 1);
}

void put_visible_char(char ch) {
  volatile uint16_t* const vga = vga_buffer();
  vga[g_console_row * kVgaColumns + g_console_column] =
      vga_cell(ch, g_console_color);

  ++g_console_column;
  if (g_console_column >= kVgaColumns) {
    g_console_column = 0;
    ++g_console_row;
    scroll_if_needed();
  }
}

void newline() {
  g_console_column = 0;
  ++g_console_row;
  scroll_if_needed();
}

void backspace() {
  if (g_console_row == g_console_start_row && g_console_column == 0) {
    return;  // 已经退到控制台区域最开始了，就不要再往前删了。
  }

  if (g_console_column == 0) {
    --g_console_row;
    g_console_column = static_cast<uint16_t>(kVgaColumns - 1);
  } else {
    --g_console_column;
  }

  volatile uint16_t* const vga = vga_buffer();
  vga[g_console_row * kVgaColumns + g_console_column] =
      vga_cell(' ', g_console_color);
}

bool is_printable_ascii(char ch) {
  return ch >= 0x20 && ch <= 0x7E;
}

}  // namespace

void initialize_console(uint16_t start_row, uint8_t color) {
  if (start_row >= kVgaRows) {
    start_row = static_cast<uint16_t>(kVgaRows - 1);
  }

  g_console_start_row = start_row;
  g_console_row = start_row;
  g_console_column = 0;
  g_console_color = color;

  for (uint16_t row = start_row; row < kVgaRows; ++row) {
    clear_row(row);
  }
}

void console_write_char(char ch) {
  if (ch == '\n') {
    newline();
    return;
  }

  if (ch == '\b') {
    backspace();
    return;
  }

  if (!is_printable_ascii(ch)) {
    return;  // 第一版控制台只直接显示可打印 ASCII。
  }

  put_visible_char(ch);
}

void console_write_string(const char* text) {
  if (text == nullptr) {
    return;
  }

  for (size_t i = 0; text[i] != '\0'; ++i) {
    console_write_char(text[i]);
  }
}

size_t console_read_line(char* buffer, size_t capacity) {
  if (buffer == nullptr || capacity < 2) {
    return 0;
  }

  size_t length = 0;

  for (;;) {
    char ch = '\0';

    while (!keyboard_try_read_char(&ch)) {
      // 这一轮先用最直接的阻塞方式：
      // 没有字符就 `hlt` 睡眠，等下一次中断把 CPU 唤醒后再试。
      wait_for_interrupt();
    }

    if (ch == '\b') {
      if (length > 0) {
        --length;
        console_write_char('\b');
      }
      continue;
    }

    if (ch == '\n') {
      console_write_char('\n');
      buffer[length] = '\0';
      return length;
    }

    if (!is_printable_ascii(ch)) {
      continue;
    }

    if (length + 1 >= capacity) {
      continue;  // 缓冲区满了就先忽略后续字符，保证结尾 '\0' 还有位置可写。
    }

    buffer[length++] = ch;
    console_write_char(ch);
  }
}
