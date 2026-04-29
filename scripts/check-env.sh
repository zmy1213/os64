#!/usr/bin/env bash
set -euo pipefail

# Small helpers keep the output readable when the script checks many tools.
pass() {
  printf '[PASS] %s\n' "$1"
}

warn() {
  printf '[WARN] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
}

# Most checks only need to know whether a command is resolvable in PATH.
check_cmd() {
  local name="$1"
  if command -v "$name" >/dev/null 2>&1; then
    pass "$name -> $(command -v "$name")"
    return 0
  fi
  fail "$name not found"
  return 1
}

echo 'os64 raw-boot environment check'

# Any missing hard requirement flips the final exit status to non-zero.
status=0

# Bootloader assembly and emulator are the minimum hard requirements.
check_cmd nasm || status=1
check_cmd qemu-system-x86_64 || status=1

# Either clang++ or g++ is enough for the later freestanding kernel stages.
if command -v clang++ >/dev/null 2>&1; then
  pass "clang++ -> $(command -v clang++)"
elif command -v g++ >/dev/null 2>&1; then
  pass "g++ -> $(command -v g++)"
else
  fail 'no C++ compiler found'
  status=1
fi

# Raw x86_64 kernel linking needs an ELF linker, not the default macOS linker.
if command -v ld.lld >/dev/null 2>&1; then
  pass "ld.lld -> $(command -v ld.lld)"
elif command -v x86_64-elf-ld >/dev/null 2>&1; then
  pass "x86_64-elf-ld -> $(command -v x86_64-elf-ld)"
else
  fail 'missing ELF linker: need ld.lld or x86_64-elf-ld'
  status=1
fi

# objcopy becomes useful once we start producing ELF or flat kernel binaries.
if command -v llvm-objcopy >/dev/null 2>&1; then
  pass "llvm-objcopy -> $(command -v llvm-objcopy)"
elif command -v objcopy >/dev/null 2>&1; then
  pass "objcopy -> $(command -v objcopy)"
else
  warn 'no objcopy found yet'
fi

# lldb is good enough for now, but a real gdb also works if installed later.
if command -v gdb >/dev/null 2>&1; then
  pass "gdb -> $(command -v gdb)"
elif command -v lldb >/dev/null 2>&1; then
  warn "lldb only -> $(command -v lldb)"
else
  warn 'no debugger found'
fi

# These base tools are used by the image-build scripts.
check_cmd dd || status=1
check_cmd truncate || status=1
check_cmd hexdump || status=1

# The final summary matches the script exit code so CI/manual checks agree.
if [ "$status" -eq 0 ]; then
  pass 'environment is ready for stage1/stage2 raw boot work'
else
  fail 'environment is not fully ready'
fi

exit "$status"
