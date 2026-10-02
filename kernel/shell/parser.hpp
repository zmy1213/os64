#ifndef OS64_SHELL_PARSER_HPP
#define OS64_SHELL_PARSER_HPP

#include <stddef.h>
#include <stdint.h>

// Fixed limits make parsing independent of the heap and bound every copy.
constexpr size_t kShellMaxCommands = 8;
constexpr size_t kShellMaxPipelineCommands = 4;
constexpr size_t kShellMaxArguments = 8;
constexpr size_t kShellArgumentBytes = 64;
constexpr size_t kShellMaxInputBytes = 512;

enum ShellCondition : uint8_t { kShellAlways, kShellOnSuccess, kShellOnFailure };
struct ShellParsedCommand {
  size_t argc;
  char argv[kShellMaxArguments][kShellArgumentBytes];
  char input[kShellArgumentBytes];
  char output[kShellArgumentBytes];
  char error[kShellArgumentBytes];
  bool output_append;
  bool error_append;
};
struct ShellParsedPipeline {
  size_t first;
  size_t count;
  ShellCondition condition;
  bool background;
};
struct ShellParsedLine {
  ShellParsedCommand commands[kShellMaxCommands];
  ShellParsedPipeline pipelines[kShellMaxCommands];
  size_t command_count;
  size_t pipeline_count;
};
struct ShellExpansion {
  const char* cwd;
  uint32_t last_status;
  bool defer = false;  // The executor expands these immediately before each pipeline.
};

// Parse the entire line before opening files or starting any process.
// Words support single/double quotes, escaping, $PWD, $PATH and $?.
// This is a documented shell subset, not a POSIX shell implementation.
bool shell_parse_line(const char* line, const ShellExpansion& expansion,
                      ShellParsedLine* result, const char** error);
bool shell_expand_command(ShellParsedCommand* command,
                          const ShellExpansion& expansion);

#endif
