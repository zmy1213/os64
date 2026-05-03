#include "console/console.hpp"

#include "interrupts/interrupts.hpp"
#include "interrupts/keyboard.hpp"
#include "task/scheduler.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;    // VGA 文本模式固定每行 80 列。
constexpr uint16_t kVgaRows = 25;       // VGA 文本模式固定 25 行。
constexpr uintptr_t kVgaBase = 0xB8000; // VGA 文本缓冲区物理地址。
constexpr size_t kConsoleEditorDraftCapacity = 128;  // 当前 shell/console 的行缓冲都很小，这个固定草稿槽位已经够用。

uint16_t g_console_start_row = 0;       // 控制台自己的显示区域从哪一行开始。
uint16_t g_console_row = 0;             // 当前光标所在行。
uint16_t g_console_column = 0;          // 当前光标所在列。
uint8_t g_console_color = 0x07;         // 当前字符颜色，默认改成浅灰字黑底，比纯白更柔和。
bool g_console_initialized = false;     // 让更早期模块能知道“VGA 控制台是否已经按自己的显示区域初始化完了”。

struct ConsoleCursor {
  uint16_t row;
  uint16_t column;
};

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

ConsoleCursor current_cursor() {
  ConsoleCursor cursor;
  cursor.row = g_console_row;
  cursor.column = g_console_column;
  return cursor;
}

void set_cursor(const ConsoleCursor& cursor) {
  g_console_row = cursor.row;
  g_console_column = cursor.column;
}

void set_cursor_to_line_offset(const ConsoleCursor& line_start,
                               size_t offset) {
  // 这一轮编辑器先明确只覆盖“不会换行的一行输入”。
  // 当前项目里 shell/console 的读行缓冲都很短，所以这条假设现在成立。
  g_console_row = line_start.row;
  g_console_column =
      static_cast<uint16_t>(line_start.column + offset);
}

void redraw_input_line(const ConsoleCursor& line_start,
                       const char* buffer,
                       size_t length,
                       size_t cursor,
                       size_t* rendered_length) {
  if (rendered_length == nullptr) {
    return;
  }

  size_t total_cells = length;
  if (*rendered_length > total_cells) {
    total_cells = *rendered_length;
  }

  set_cursor(line_start);
  for (size_t i = 0; i < total_cells; ++i) {
    const char ch = (i < length) ? buffer[i] : ' ';
    put_visible_char(ch);
  }

  set_cursor_to_line_offset(line_start, cursor);
  *rendered_length = length;
}

void copy_line_text(char* destination,
                    size_t capacity,
                    size_t* out_length,
                    const char* source) {
  if (destination == nullptr || capacity < 2 || out_length == nullptr) {
    return;
  }

  size_t length = 0;
  if (source != nullptr) {
    while (source[length] != '\0' && (length + 1) < capacity) {
      destination[length] = source[length];
      ++length;
    }
  }

  destination[length] = '\0';
  *out_length = length;
}

size_t history_entry_count(const ConsoleHistoryProvider* history) {
  if (history == nullptr || history->entry_count == nullptr) {
    return 0;
  }

  return history->entry_count(history->context);
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

  g_console_initialized = true;
}

bool console_is_initialized() {
  return g_console_initialized;
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

void console_set_color(uint8_t color) {
  g_console_color = color;
}

void console_clear() {
  for (uint16_t row = g_console_start_row; row < kVgaRows; ++row) {
    clear_row(row);
  }

  g_console_row = g_console_start_row;
  g_console_column = 0;
}

size_t console_read_line(char* buffer, size_t capacity) {
  return console_read_line_with_history(buffer, capacity, nullptr);
}

size_t console_read_line_with_history(char* buffer,
                                      size_t capacity,
                                      const ConsoleHistoryProvider* history) {
  if (buffer == nullptr || capacity < 2) {
    return 0;
  }

  size_t length = 0;
  size_t cursor = 0;
  size_t rendered_length = 0;
  const ConsoleCursor line_start = current_cursor();
  const size_t available_history_count = history_entry_count(history);
  size_t history_cursor = available_history_count;  // 指向“当前正在看哪条历史”；等于 count 时表示还在新输入草稿上。
  char draft_buffer[kConsoleEditorDraftCapacity];
  size_t draft_length = 0;

  buffer[0] = '\0';
  draft_buffer[0] = '\0';

  for (;;) {
    KeyboardInputEvent event;
    event.kind = kKeyboardInputCharacter;
    event.character = '\0';

    while (!keyboard_try_read_input_event(&event)) {
      // 这一轮先用最直接的阻塞方式：
      // 没有字符就 `hlt` 睡眠，等下一次中断把 CPU 唤醒后再试。
      wait_for_interrupt();

      // 如果 timer 已经把时间片用尽的请求挂起来了，
      // 那就趁这个等待输入的安全点把 CPU 让给别的线程。
      (void)scheduler_yield_if_requested();
    }

    if (event.kind == kKeyboardInputArrowLeft) {
      if (cursor > 0) {
        --cursor;
        set_cursor_to_line_offset(line_start, cursor);
      }
      continue;
    }

    if (event.kind == kKeyboardInputArrowRight) {
      if (cursor < length) {
        ++cursor;
        set_cursor_to_line_offset(line_start, cursor);
      }
      continue;
    }

    if (event.kind == kKeyboardInputHome) {
      cursor = 0;
      set_cursor_to_line_offset(line_start, cursor);
      continue;
    }

    if (event.kind == kKeyboardInputEnd) {
      cursor = length;
      set_cursor_to_line_offset(line_start, cursor);
      continue;
    }

    if (event.kind == kKeyboardInputDelete) {
      if (cursor < length) {
        for (size_t i = cursor; i < length; ++i) {
          buffer[i] = buffer[i + 1];
        }
        --length;
        redraw_input_line(line_start, buffer, length, cursor,
                          &rendered_length);
      }
      continue;
    }

    if (event.kind == kKeyboardInputArrowUp) {
      if (available_history_count == 0 ||
          history == nullptr ||
          history->entry_text == nullptr) {
        continue;
      }

      if (history_cursor == available_history_count) {
        copy_line_text(draft_buffer, sizeof(draft_buffer), &draft_length,
                       buffer);
      }

      if (history_cursor == 0) {
        continue;
      }

      --history_cursor;
      copy_line_text(buffer, capacity, &length,
                     history->entry_text(history->context, history_cursor));
      cursor = length;
      redraw_input_line(line_start, buffer, length, cursor, &rendered_length);
      continue;
    }

    if (event.kind == kKeyboardInputArrowDown) {
      if (available_history_count == 0 ||
          history == nullptr ||
          history->entry_text == nullptr ||
          history_cursor >= available_history_count) {
        continue;
      }

      ++history_cursor;
      if (history_cursor == available_history_count) {
        copy_line_text(buffer, capacity, &length, draft_buffer);
      } else {
        copy_line_text(buffer, capacity, &length,
                       history->entry_text(history->context, history_cursor));
      }

      cursor = length;
      redraw_input_line(line_start, buffer, length, cursor, &rendered_length);
      continue;
    }

    if (event.kind != kKeyboardInputCharacter) {
      continue;
    }

    const char ch = event.character;

    if (ch == '\b') {
      if (cursor > 0) {
        for (size_t i = cursor - 1; i < length; ++i) {
          buffer[i] = buffer[i + 1];
        }
        --cursor;
        --length;
        redraw_input_line(line_start, buffer, length, cursor,
                          &rendered_length);
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

    for (size_t i = length; i > cursor; --i) {
      buffer[i] = buffer[i - 1];
    }
    buffer[cursor] = ch;
    ++length;
    ++cursor;
    buffer[length] = '\0';
    redraw_input_line(line_start, buffer, length, cursor, &rendered_length);
  }
}
