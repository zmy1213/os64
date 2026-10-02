#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR/user"
rm -f "$BUILD_DIR/user/"*.elf "$BUILD_DIR/user/"*.o
USER_FLAGS=( -I "$ROOT_DIR/user" -ffreestanding -fno-exceptions -fno-rtti
  -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only
  -fno-builtin
  -Os -Wall -Wextra -fno-asynchronous-unwind-tables -fno-unwind-tables )
if [[ "$CLANGXX_BIN" == *clang++* ]]; then
  USER_FLAGS=( --target=x86_64-elf "${USER_FLAGS[@]}" )
fi
"$NASM_BIN" -f elf64 "$ROOT_DIR/user/start.asm" -o "$BUILD_DIR/user/start.o"
"$CLANGXX_BIN" "${USER_FLAGS[@]}" -c "$ROOT_DIR/user/memory.cpp" -o "$BUILD_DIR/user/memory.o"
for source in "$ROOT_DIR"/user/programs/*.cpp; do
  name="$(basename "${source%.cpp}")"
  "$CLANGXX_BIN" "${USER_FLAGS[@]}" -c "$source" -o "$BUILD_DIR/user/$name.o"
  "$LD_BIN" -m elf_x86_64 -nostdlib -z max-page-size=0x8 \
    -T "$ROOT_DIR/user/user.ld" -o "$BUILD_DIR/user/$name.unstripped.elf" \
    "$BUILD_DIR/user/start.o" "$BUILD_DIR/user/$name.o" "$BUILD_DIR/user/memory.o"
  "$OBJCOPY_BIN" --strip-all "$BUILD_DIR/user/$name.unstripped.elf" "$BUILD_DIR/user/$name.elf"
done
