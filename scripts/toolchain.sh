#!/usr/bin/env bash
# Shared discovery supports PATH on Linux and opt-only Homebrew tools on macOS.
find_tool() {
  local candidate resolved
  for candidate in "$@"; do
    if resolved="$(command -v "$candidate" 2>/dev/null)"; then
      printf '%s\n' "$resolved"
      return 0
    fi
    if [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  printf 'Missing tool: %s\n' "$*" >&2
  return 1
}
resolve_build_tools() {
  NASM_BIN="${NASM_BIN:-$(find_tool nasm /opt/homebrew/bin/nasm /usr/local/bin/nasm)}"
  CLANGXX_BIN="${CLANGXX_BIN:-$(find_tool clang++ x86_64-elf-g++ /opt/homebrew/opt/llvm/bin/clang++)}"
  LD_BIN="${LD_BIN:-$(find_tool ld.lld x86_64-elf-ld /opt/homebrew/opt/lld/bin/ld.lld /usr/local/opt/llvm/bin/ld.lld)}"
  OBJCOPY_BIN="${OBJCOPY_BIN:-$(find_tool llvm-objcopy x86_64-elf-objcopy objcopy /opt/homebrew/opt/llvm/bin/llvm-objcopy /usr/local/opt/llvm/bin/llvm-objcopy)}"
  PYTHON_BIN="${PYTHON_BIN:-$(find_tool python3 /opt/homebrew/bin/python3)}"
  export NASM_BIN CLANGXX_BIN LD_BIN OBJCOPY_BIN PYTHON_BIN
}
resolve_qemu() {
  QEMU_BIN="${QEMU_BIN:-${QEMU:-$(find_tool qemu-system-x86_64 /opt/homebrew/bin/qemu-system-x86_64 /usr/local/bin/qemu-system-x86_64)}}"
  export QEMU_BIN
}
