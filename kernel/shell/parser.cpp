#include "shell/parser.hpp"

namespace {
enum TokenKind { End, Word, Pipe, Semicolon, Background, And, Or,
                 Input, Output, Append, ErrorOutput, ErrorAppend, Invalid };
struct Token { TokenKind kind; char text[kShellArgumentBytes]; };
bool space(char c) { return c == ' ' || c == '\t'; }
bool name_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_';
}
bool equal(const char* a, const char* b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
bool append(char* target, size_t& size, char c) {
  if (size + 1 >= kShellArgumentBytes) { return false; }
  target[size++] = c; target[size] = 0; return true;
}
bool append_text(char* target, size_t& size, const char* value) {
  if (!value) { return true; }
  while (*value) { if (!append(target, size, *value++)) { return false; } }
  return true;
}
void copy(char* target, const char* source) {
  while ((*target++ = *source++) != 0) {}
}
bool expand(const char*& cursor, const ShellExpansion& variables,
            char* text, size_t& length, const char** error) {
  ++cursor;
  if (*cursor == '?') {
    ++cursor;
    if (variables.defer) { return append(text, length, '\x1f') && append(text, length, 'S'); }
    char digits[11]; size_t n = 0; uint32_t value = variables.last_status;
    do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    while (n) { if (!append(text, length, digits[--n])) { return false; } }
    return true;
  }
  const bool braced = *cursor == '{';
  if (braced) { ++cursor; }
  char name[kShellArgumentBytes] = {}; size_t n = 0;
  while (name_char(*cursor)) {
    if (!append(name, n, *cursor++)) { return false; }
  }
  if (braced) {
    if (*cursor != '}') { *error = "missing } in variable"; return false; }
    ++cursor;
  }
  if (n == 0 && !braced) { return append(text, length, '$'); }
  if (equal(name, "PWD")) {
    if (variables.defer) { return append(text, length, '\x1f') && append(text, length, 'D'); }
    return append_text(text, length, variables.cwd);
  }
  if (equal(name, "PATH")) { return append_text(text, length, "/bin"); }
  // No environment table yet: unknown names are unset (empty).
  return true;
}
Token next(const char*& cursor, const ShellExpansion& variables, const char** error) {
  Token token = {}; token.kind = End;
  while (space(*cursor)) { ++cursor; }
  if (*cursor == 0 || *cursor == '#') { return token; }
  switch (*cursor) {
    case '|': ++cursor; token.kind = *cursor == '|' ? (++cursor, Or) : Pipe; return token;
    case '&': ++cursor; token.kind = *cursor == '&' ? (++cursor, And) : Background; return token;
    case ';': ++cursor; token.kind = Semicolon; return token;
    case '<': ++cursor; token.kind = Input; return token;
    case '>': ++cursor; token.kind = *cursor == '>' ? (++cursor, Append) : Output; return token;
    case '2':
      if (cursor[1] == '>') {
        cursor += 2; token.kind = *cursor == '>' ? (++cursor, ErrorAppend) : ErrorOutput;
        return token;
      }
      break;
  }
  token.kind = Word; size_t length = 0; char quote = 0; bool started = false;
  while (*cursor) {
    char c = *cursor;
    if (quote == 0 && (space(c) || c == '|' || c == '&' || c == ';' || c == '<' || c == '>')) { break; }
    ++cursor; started = true;
    if (c == '\'' && quote != '"') { quote = quote == '\'' ? 0 : '\''; continue; }
    if (c == '"' && quote != '\'') { quote = quote == '"' ? 0 : '"'; continue; }
    if (c == '\\' && quote != '\'') {
      if (*cursor == 0) { *error = "trailing escape"; token.kind = Invalid; return token; }
      if (quote == '"' && *cursor != '$' && *cursor != '"' && *cursor != '\\') {
        if (!append(token.text, length, c)) { *error = "word exceeds 63 bytes"; token.kind = Invalid; return token; }
        continue;
      }
      c = *cursor++;
    } else if (c == '$' && quote != '\'') {
      --cursor;
      if (!expand(cursor, variables, token.text, length, error)) {
        token.kind = Invalid; if (!*error) { *error = "word exceeds 63 bytes"; } return token;
      }
      continue;
    }
    if (!append(token.text, length, c)) { *error = "word exceeds 63 bytes"; token.kind = Invalid; return token; }
  }
  if (quote) { *error = "unterminated quote"; token.kind = Invalid; }
  if (!started) { token.kind = Invalid; *error = "invalid token"; }
  return token;
}
}

bool shell_parse_line(const char* line, const ShellExpansion& expansion,
                      ShellParsedLine* result, const char** error) {
  if (!line || !result || !error) { return false; }
  *result = {}; *error = nullptr;
  size_t input_length = 0;
  while (line[input_length]) {
    if (static_cast<uint8_t>(line[input_length]) < 32 && line[input_length] != '\t') {
      *error = "control characters are not allowed"; return false;
    }
    if (++input_length > kShellMaxInputBytes) { *error = "line exceeds 512 bytes"; return false; }
  }
  const char* cursor = line; Token token = next(cursor, expansion, error);
  if (token.kind == End) { return true; }
  ShellCondition condition = kShellAlways;
  while (token.kind != End) {
    auto& pipeline = result->pipelines[result->pipeline_count++];
    pipeline.first = result->command_count; pipeline.condition = condition;
    while (true) {
      if (result->command_count == kShellMaxCommands || pipeline.count == kShellMaxPipelineCommands) {
        *error = "limit: 8 commands per line, 4 per pipeline"; return false;
      }
      auto& command = result->commands[result->command_count++]; ++pipeline.count;
      while (token.kind == Word || (token.kind >= Input && token.kind <= ErrorAppend)) {
        if (token.kind == Word) {
          if (command.argc == kShellMaxArguments) { *error = "limit: 8 arguments per command"; return false; }
          copy(command.argv[command.argc++], token.text);
        } else {
          const TokenKind redirect = token.kind; token = next(cursor, expansion, error);
          if (token.kind != Word || token.text[0] == 0) { *error = "redirect needs a filename"; return false; }
          char* destination = redirect == Input ? command.input :
              (redirect == ErrorOutput || redirect == ErrorAppend) ? command.error : command.output;
          if (*destination) { *error = "duplicate redirect for the same stream"; return false; }
          copy(destination, token.text);
          if (redirect == Append) { command.output_append = true; }
          if (redirect == ErrorAppend) { command.error_append = true; }
        }
        token = next(cursor, expansion, error);
      }
      if (token.kind == Invalid) { return false; }
      if (!command.argc || !command.argv[0][0]) { *error = "missing command"; return false; }
      if (token.kind != Pipe) { break; }
      token = next(cursor, expansion, error);
    }
    if (token.kind == End) { return true; }
    condition = token.kind == And ? kShellOnSuccess : token.kind == Or ? kShellOnFailure : kShellAlways;
    if (token.kind == Background) { pipeline.background = true; }
    else if (token.kind != And && token.kind != Or && token.kind != Semicolon) {
      *error = "unexpected operator"; return false;
    }
    const TokenKind separator = token.kind; token = next(cursor, expansion, error);
    if (token.kind == End && (separator == And || separator == Or)) { *error = "missing command after condition"; return false; }
    if (result->pipeline_count == kShellMaxCommands && token.kind != End) { *error = "too many pipelines"; return false; }
  }
  return true;
}

bool shell_expand_command(ShellParsedCommand* command,
                          const ShellExpansion& expansion) {
  if (!command) { return false; }
  char* fields[kShellMaxArguments + 3]; size_t count = 0;
  for (size_t i = 0; i < command->argc; ++i) { fields[count++] = command->argv[i]; }
  fields[count++] = command->input; fields[count++] = command->output; fields[count++] = command->error;
  for (size_t i = 0; i < count; ++i) {
    char expanded[kShellArgumentBytes] = {}; size_t length = 0;
    const char* cursor = fields[i];
    while (*cursor) {
      if (*cursor != '\x1f') {
        if (!append(expanded, length, *cursor++)) { return false; }
        continue;
      }
      ++cursor; const char marker = *cursor++;
      if (marker == 'D') {
        if (!append_text(expanded, length, expansion.cwd)) { return false; }
      } else if (marker == 'S') {
        const char* variable = "$?"; const char* error = nullptr;
        if (!expand(variable, expansion, expanded, length, &error)) { return false; }
      } else { return false; }
    }
    copy(fields[i], expanded);
  }
  return true;
}
