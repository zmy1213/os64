#include "shell/shell.hpp"

#include "console/console.hpp"
#include "interrupts/pit.hpp"

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

bool strings_equal(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return left == right;
  }

  for (size_t i = 0;; ++i) {
    if (left[i] != right[i]) {
      return false;
    }

    if (left[i] == '\0') {
      return true;
    }
  }
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

}  // namespace

bool initialize_shell(ShellState* shell,
                      const PageAllocator* allocator,
                      const ShellOutput* output) {
  if (shell == nullptr || output == nullptr || output->write_char == nullptr) {
    return false;
  }

  shell->allocator = allocator;
  shell->output = *output;
  return true;
}

void shell_print_prompt(const ShellState* shell) {
  write_string(shell, "os64> ");
}

ShellCommandResult shell_execute_line(const ShellState* shell,
                                      const char* line) {
  if (line == nullptr || line[0] == '\0') {
    return kShellCommandEmpty;
  }

  if (strings_equal(line, "help")) {
    handle_help_command(shell);
    return kShellCommandExecuted;
  }

  if (strings_equal(line, "mem")) {
    handle_mem_command(shell);
    return kShellCommandExecuted;
  }

  if (strings_equal(line, "ticks")) {
    handle_ticks_command(shell);
    return kShellCommandExecuted;
  }

  write_string(shell, "unknown command: ");
  write_string(shell, line);
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

void shell_run_forever(const ShellState* shell,
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
