#include "shell/shell.hpp"

#include "console/console.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"

namespace {

void write_char(const ShellState* shell, char ch) {
  if (shell == nullptr || shell->output.write_char == nullptr) {
    return;
  }

  shell->output.write_char(ch);
}

void write_string(const ShellState* shell, const char* text) {
  if (text == nullptr) {
    return;
  }

  for (size_t i = 0; text[i] != '\0'; ++i) {
    write_char(shell, text[i]);
  }
}

void write_newline(const ShellState* shell) {
  write_char(shell, '\n');
}

void clear_output(const ShellState* shell) {
  if (shell == nullptr || shell->output.clear == nullptr) {
    return;
  }

  shell->output.clear();
}

void write_u64(const ShellState* shell, uint64_t value) {
  char digits[20];
  size_t count = 0;

  if (value == 0) {
    write_char(shell, '0');
    return;
  }

  while (value != 0 && count < sizeof(digits)) {
    digits[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }

  while (count > 0) {
    write_char(shell, digits[--count]);
  }
}

bool is_space_char(char ch) {
  return ch == ' ' || ch == '\t';
}

const char* skip_spaces(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }

  while (is_space_char(*text)) {
    ++text;
  }

  return text;
}

bool is_empty_after_trim(const char* text) {
  const char* trimmed = skip_spaces(text);
  return trimmed == nullptr || trimmed[0] == '\0';
}

size_t string_length(const char* text) {
  if (text == nullptr) {
    return 0;
  }

  size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }

  return length;
}

const char* trim_trailing_spaces(const char* begin, const char* end) {
  while (end > begin && is_space_char(end[-1])) {
    --end;
  }

  return end;
}

void record_history_line(ShellState* shell, const char* line) {
  if (shell == nullptr || line == nullptr) {
    return;
  }

  const char* trimmed_begin = skip_spaces(line);
  if (trimmed_begin == nullptr || trimmed_begin[0] == '\0') {
    return;
  }

  const char* trimmed_end = trimmed_begin + string_length(trimmed_begin);
  trimmed_end = trim_trailing_spaces(trimmed_begin, trimmed_end);
  if (trimmed_end <= trimmed_begin) {
    return;
  }

  const size_t slot = shell->history_next_slot;
  const size_t line_length =
      static_cast<size_t>(trimmed_end - trimmed_begin);
  const size_t copy_length =
      (line_length < (kShellHistoryEntryCapacity - 1))
          ? line_length
          : (kShellHistoryEntryCapacity - 1);

  // 历史记录先直接写进固定槽位。
  // 这一轮不走堆分配，而是故意用固定容量 ring buffer 保持行为可预测。
  for (size_t i = 0; i < copy_length; ++i) {
    shell->history_entries[slot][i] = trimmed_begin[i];
  }
  shell->history_entries[slot][copy_length] = '\0';

  shell->history_sequence_numbers[slot] = shell->history_total_count + 1;
  ++shell->history_total_count;

  if (shell->history_count < kShellHistoryCapacity) {
    ++shell->history_count;
  }

  shell->history_next_slot =
      static_cast<uint16_t>((slot + 1) % kShellHistoryCapacity);
}

// 这就是这一轮最关键的小升级：
// 以前 shell 只能看“整行是不是刚好等于 help/mem/ticks”；
// 现在它先识别命令名，再把后面的剩余部分当成参数区。
bool command_matches(const char* line,
                     const char* command,
                     const char** out_args) {
  if (line == nullptr || command == nullptr) {
    return false;
  }

  const char* cursor = skip_spaces(line);
  size_t index = 0;

  while (command[index] != '\0') {
    if (cursor[index] != command[index]) {
      return false;
    }

    ++index;
  }

  if (cursor[index] == '\0') {
    if (out_args != nullptr) {
      *out_args = cursor + index;
    }
    return true;
  }

  if (!is_space_char(cursor[index])) {
    return false;
  }

  if (out_args != nullptr) {
    *out_args = skip_spaces(cursor + index);
  }

  return true;
}

void handle_help_command(const ShellState* shell) {
  write_string(shell, "commands:");
  write_newline(shell);
  write_string(shell, "help  - list commands");
  write_newline(shell);
  write_string(shell, "mem   - show free physical pages");
  write_newline(shell);
  write_string(shell, "ticks - show timer tick count");
  write_newline(shell);
  write_string(shell, "heap  - show kernel heap stats");
  write_newline(shell);
  write_string(shell, "irq   - show timer/keyboard irq stats");
  write_newline(shell);
  write_string(shell, "uptime - show tick-based uptime");
  write_newline(shell);
  write_string(shell, "echo  - print text back");
  write_newline(shell);
  write_string(shell, "history - show recent commands");
  write_newline(shell);
  write_string(shell, "clear - clear console area");
  write_newline(shell);
}

void handle_mem_command(const ShellState* shell) {
  if (shell == nullptr || shell->allocator == nullptr) {
    write_string(shell, "mem unavailable");
    write_newline(shell);
    return;
  }

  const uint64_t free_pages = count_free_pages(shell->allocator);
  const uint64_t free_bytes = free_pages * kPageSize;

  write_string(shell, "mem_free_pages=");
  write_u64(shell, free_pages);
  write_newline(shell);

  write_string(shell, "mem_free_bytes=");
  write_u64(shell, free_bytes);
  write_newline(shell);
}

void handle_ticks_command(const ShellState* shell) {
  write_string(shell, "ticks_current=");
  write_u64(shell, timer_tick_count());
  write_newline(shell);
}

void handle_heap_command(const ShellState* shell) {
  if (shell == nullptr || shell->heap == nullptr) {
    write_string(shell, "heap unavailable");
    write_newline(shell);
    return;
  }

  write_string(shell, "heap_used_bytes=");
  write_u64(shell, heap_used_bytes(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_mapped_bytes=");
  write_u64(shell, heap_mapped_bytes(shell->heap));
  write_newline(shell);

  write_string(shell, "heap_free_bytes=");
  write_u64(shell, heap_free_bytes(shell->heap));
  write_newline(shell);
}

void handle_irq_command(const ShellState* shell) {
  write_string(shell, "irq_timer_ticks=");
  write_u64(shell, timer_tick_count());
  write_newline(shell);

  write_string(shell, "irq_timer_frequency_hz=");
  write_u64(shell, timer_frequency_hz());
  write_newline(shell);

  write_string(shell, "irq_keyboard_count=");
  write_u64(shell, keyboard_irq_count());
  write_newline(shell);

  write_string(shell, "irq_keyboard_buffered_chars=");
  write_u64(shell, keyboard_buffered_char_count());
  write_newline(shell);

  write_string(shell, "irq_keyboard_dropped_chars=");
  write_u64(shell, keyboard_dropped_char_count());
  write_newline(shell);
}

void handle_uptime_command(const ShellState* shell) {
  const uint64_t ticks = timer_tick_count();
  const uint64_t frequency_hz = timer_frequency_hz();

  write_string(shell, "uptime_ticks=");
  write_u64(shell, ticks);
  write_newline(shell);

  write_string(shell, "uptime_frequency_hz=");
  write_u64(shell, frequency_hz);
  write_newline(shell);

  if (frequency_hz == 0) {
    write_string(shell, "uptime_ms=0");
    write_newline(shell);
    return;
  }

  write_string(shell, "uptime_ms=");
  write_u64(shell, (ticks * 1000) / frequency_hz);
  write_newline(shell);
}

void handle_echo_command(const ShellState* shell, const char* arguments) {
  const char* text = skip_spaces(arguments);
  if (text != nullptr) {
    write_string(shell, text);
  }

  write_newline(shell);
}

void handle_history_command(const ShellState* shell) {
  if (shell == nullptr) {
    return;
  }

  write_string(shell, "history_buffered=");
  write_u64(shell, shell->history_count);
  write_newline(shell);

  write_string(shell, "history_total=");
  write_u64(shell, shell->history_total_count);
  write_newline(shell);

  if (shell->history_count == 0) {
    write_string(shell, "history empty");
    write_newline(shell);
    return;
  }

  size_t slot = 0;
  if (shell->history_count == kShellHistoryCapacity) {
    // 写满以后，`history_next_slot` 指向“下一次要覆盖谁”，
    // 也就等于“当前最旧的一条命令在哪个槽位”。
    slot = shell->history_next_slot;
  }

  for (size_t i = 0; i < shell->history_count; ++i) {
    write_string(shell, "history[");
    write_u64(shell, shell->history_sequence_numbers[slot]);
    write_string(shell, "]=");
    write_string(shell, shell->history_entries[slot]);
    write_newline(shell);

    slot = (slot + 1) % kShellHistoryCapacity;
  }
}

}  // namespace

bool initialize_shell(ShellState* shell,
                      const PageAllocator* allocator,
                      const KernelHeap* heap,
                      const ShellOutput* output) {
  if (shell == nullptr || output == nullptr || output->write_char == nullptr) {
    return false;
  }

  shell->allocator = allocator;
  shell->heap = heap;
  shell->output = *output;
  shell->history_count = 0;
  shell->history_next_slot = 0;
  shell->history_total_count = 0;
  memory_set(shell->history_sequence_numbers, 0,
             sizeof(shell->history_sequence_numbers));
  memory_set(shell->history_entries, 0, sizeof(shell->history_entries));
  return true;
}

void shell_print_prompt(const ShellState* shell) {
  write_string(shell, "os64> ");
}

ShellCommandResult shell_execute_line(ShellState* shell,
                                      const char* line) {
  const char* trimmed_line = skip_spaces(line);
  if (trimmed_line == nullptr || trimmed_line[0] == '\0') {
    return kShellCommandEmpty;
  }

  // 先记历史，再执行命令。
  // 这样 `history` 自己也会出现在当前历史列表里，更符合直觉。
  record_history_line(shell, trimmed_line);

  const char* arguments = nullptr;

  if (command_matches(trimmed_line, "help", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_help_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "mem", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_mem_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "ticks", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_ticks_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "heap", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_heap_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "irq", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_irq_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "uptime", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_uptime_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "echo", &arguments)) {
    handle_echo_command(shell, arguments);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "history", &arguments) &&
      is_empty_after_trim(arguments)) {
    handle_history_command(shell);
    return kShellCommandExecuted;
  }

  if (command_matches(trimmed_line, "clear", &arguments) &&
      is_empty_after_trim(arguments)) {
    clear_output(shell);
    return kShellCommandExecuted;
  }

  write_string(shell, "unknown command: ");
  write_string(shell, trimmed_line);
  write_newline(shell);
  return kShellCommandUnknown;
}

const char* shell_command_result_name(ShellCommandResult result) {
  switch (result) {
    case kShellCommandEmpty:
      return "empty";
    case kShellCommandExecuted:
      return "executed";
    case kShellCommandUnknown:
      return "unknown";
    default:
      return "invalid";
  }
}

void shell_run_forever(ShellState* shell,
                       char* line_buffer,
                       size_t capacity) {
  if (shell == nullptr || line_buffer == nullptr || capacity < 2) {
    return;
  }

  for (;;) {
    shell_print_prompt(shell);
    (void)console_read_line(line_buffer, capacity);
    (void)shell_execute_line(shell, line_buffer);
  }
}
