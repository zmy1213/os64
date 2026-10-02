#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR/kernel" "$BUILD_DIR/user"

# Rebuild every source: smoke-test defines must never reuse stale objects.
KERNEL_OPT_LEVEL="${KERNEL_OPT_LEVEL:-2}"
case "$KERNEL_OPT_LEVEL" in 0|1|2|3|s|g) ;; *) printf 'Invalid KERNEL_OPT_LEVEL\n' >&2; exit 1 ;; esac
NETWORK_IRQ_ENABLED="${NETWORK_IRQ_ENABLED:-1}"
case "$NETWORK_IRQ_ENABLED" in 0|1) ;; *) printf 'Invalid NETWORK_IRQ_ENABLED\n' >&2; exit 1 ;; esac
CXXFLAGS=( -I "$ROOT_DIR/kernel" -ffreestanding -fno-exceptions -fno-rtti
  -fno-stack-protector -fno-pic -fno-builtin -mno-red-zone -mgeneral-regs-only -mcmodel=kernel
  "-O$KERNEL_OPT_LEVEL" "-DOS64_NETWORK_IRQ_ENABLED=$NETWORK_IRQ_ENABLED" -Wall -Wextra )
if [[ "$CLANGXX_BIN" == *clang++* ]]; then
  CXXFLAGS=( --target=x86_64-elf "${CXXFLAGS[@]}" )
fi
EXTRA_FLAGS=()
if [ -n "${KERNEL_EXTRA_CXXFLAGS:-}" ]; then
  read -r -a EXTRA_FLAGS <<< "$KERNEL_EXTRA_CXXFLAGS"
  CXXFLAGS+=( "${EXTRA_FLAGS[@]}" )
fi
OBJECTS=()
while IFS= read -r source; do
  relative="${source#"$ROOT_DIR/kernel/"}"
  object="$BUILD_DIR/kernel/${relative%.*}.o"
  mkdir -p "$(dirname "$object")"
  "$CLANGXX_BIN" "${CXXFLAGS[@]}" -c "$source" -o "$object"
  OBJECTS+=( "$object" )
done < <(find "$ROOT_DIR/kernel" -name '*.cpp' -type f | LC_ALL=C sort)
while IFS= read -r source; do
  relative="${source#"$ROOT_DIR/kernel/"}"
  object="$BUILD_DIR/kernel/${relative%.*}.o"
  mkdir -p "$(dirname "$object")"
  "$NASM_BIN" -f elf64 "$source" -o "$object"
  OBJECTS+=( "$object" )
done < <(find "$ROOT_DIR/kernel" -name '*.asm' -type f | LC_ALL=C sort)
"$LD_BIN" -m elf_x86_64 -T "$ROOT_DIR/kernel/boot/linker.ld" \
  -o "$BUILD_DIR/kernel.elf" "${OBJECTS[@]}"
"$OBJCOPY_BIN" -O binary "$BUILD_DIR/kernel.elf" "$BUILD_DIR/kernel.bin"

# Preserve the original assembly fixtures used by teaching regressions.
"$NASM_BIN" -f bin "$ROOT_DIR/user/hello.asm" -o "$BUILD_DIR/hello.bin"
"$NASM_BIN" -f elf64 "$ROOT_DIR/user/hello_elf.asm" -o "$BUILD_DIR/hello_elf.o"
"$LD_BIN" -m elf_x86_64 -nostdlib -z max-page-size=0x8 \
  -T "$ROOT_DIR/user/hello_elf.ld" -o "$BUILD_DIR/hello.unstripped.elf" "$BUILD_DIR/hello_elf.o"
"$OBJCOPY_BIN" --strip-all "$BUILD_DIR/hello.unstripped.elf" "$BUILD_DIR/hello.elf"
BUILD_DIR="$BUILD_DIR" bash "$ROOT_DIR/scripts/build-user.sh"

"$PYTHON_BIN" "$ROOT_DIR/scripts/make-volume.py" --build-dir "$BUILD_DIR"
"$PYTHON_BIN" "$ROOT_DIR/scripts/make-boot-image.py" --build-dir "$BUILD_DIR" --metadata-only
"$NASM_BIN" -f bin "$ROOT_DIR/boot/stage1.asm" -o "$BUILD_DIR/stage1.bin"
"$NASM_BIN" -f bin -i "$BUILD_DIR/" "$ROOT_DIR/boot/stage2.asm" -o "$BUILD_DIR/stage2.bin"
"$PYTHON_BIN" "$ROOT_DIR/scripts/make-boot-image.py" --build-dir "$BUILD_DIR"

# The boot image is disposable; data.img survives ordinary builds.
if [ ! -f "$BUILD_DIR/data.img" ]; then
  cp "$BUILD_DIR/data_volume.bin" "$BUILD_DIR/data.img"
  printf 'Created persistent data disk: %s\n' "$BUILD_DIR/data.img"
fi
"$PYTHON_BIN" "$ROOT_DIR/scripts/record-build.py" "$BUILD_DIR" "$KERNEL_OPT_LEVEL" "$CLANGXX_BIN" "$NASM_BIN" "$NETWORK_IRQ_ENABLED"
printf 'Built %s/disk.img (kernel %s bytes); user tools in /bin\n' \
  "$BUILD_DIR" "$(wc -c < "$BUILD_DIR/kernel.bin" | tr -d ' ')"
