#include <cassert>
#include <cstring>
#include <iostream>
#include "shell/parser.hpp"

int main() {
  ShellParsedLine plan; const char* error = nullptr;
  auto parse = [&](const char* line) { return shell_parse_line(line, {"/docs", 7}, &plan, &error); };
  assert(parse("echo 'a | b' \"$PWD\" \\| '' | cat > /x && pwd; echo $? &"));
  assert(plan.command_count == 4 && plan.pipeline_count == 3);
  assert(plan.pipelines[0].count == 2 && plan.pipelines[1].condition == kShellOnSuccess);
  assert(plan.pipelines[2].background);
  assert(!std::strcmp(plan.commands[0].argv[1], "a | b"));
  assert(!std::strcmp(plan.commands[0].argv[2], "/docs"));
  assert(!std::strcmp(plan.commands[0].argv[3], "|"));
  assert(!std::strcmp(plan.commands[0].argv[4], ""));
  assert(!std::strcmp(plan.commands[1].output, "/x"));
  assert(parse("cat<in 2>>err >>out||echo failed # ignored |"));
  assert(plan.commands[0].error_append && plan.commands[0].output_append);
  assert(parse("echo 'literal\\$?' \"kept\\z\" ${PATH}"));
  assert(!std::strcmp(plan.commands[0].argv[1], "literal\\$?"));
  assert(!std::strcmp(plan.commands[0].argv[2], "kept\\z"));
  assert(!std::strcmp(plan.commands[0].argv[3], "/bin"));
  for (const char* invalid : {"echo 'open", "cat >", "| cat", "echo &&", "cat >a >b",
                            "echo \\", "echo a b c d e f g h", "cat|cat|cat|cat|cat", "echo ${PWD"}) {
    assert(!parse(invalid) && error);
  }
  char long_word[100]; std::memset(long_word, 'a', sizeof(long_word)); long_word[99] = 0;
  assert(!parse(long_word));
  assert(shell_parse_line("echo '$?' $? $PWD", {"/", 0, true}, &plan, &error));
  assert(shell_expand_command(&plan.commands[0], {"/changed", 42}));
  assert(!std::strcmp(plan.commands[0].argv[1], "$?"));
  assert(!std::strcmp(plan.commands[0].argv[2], "42"));
  assert(!std::strcmp(plan.commands[0].argv[3], "/changed"));
  std::cout << "shell parser host tests passed\n";
}
