#!/usr/bin/env bash
set -euo pipefail

# Resolve the repo root once so the script works from any current directory.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
STAGE1_SRC="$ROOT_DIR/boot/stage1.asm"
STAGE2_SRC="$ROOT_DIR/boot/stage2.asm"
KERNEL_INCLUDE_DIR="$ROOT_DIR/kernel"
KERNEL_ENTRY_SRC="$ROOT_DIR/kernel/boot/entry64.asm"
KERNEL_INTERRUPT_STUBS_SRC="$ROOT_DIR/kernel/interrupts/interrupt_stubs.asm"
KERNEL_MAIN_SRC="$ROOT_DIR/kernel/core/kernel_main.cpp"
KERNEL_PAGE_ALLOCATOR_SRC="$ROOT_DIR/kernel/memory/page_allocator.cpp"
KERNEL_PAGING_SRC="$ROOT_DIR/kernel/memory/paging.cpp"
KERNEL_RUNTIME_SRC="$ROOT_DIR/kernel/runtime/runtime.cpp"
KERNEL_INTERRUPTS_SRC="$ROOT_DIR/kernel/interrupts/interrupts.cpp"
KERNEL_PIC_SRC="$ROOT_DIR/kernel/interrupts/pic.cpp"
KERNEL_PIT_SRC="$ROOT_DIR/kernel/interrupts/pit.cpp"
KERNEL_KEYBOARD_SRC="$ROOT_DIR/kernel/interrupts/keyboard.cpp"
KERNEL_HEAP_SRC="$ROOT_DIR/kernel/memory/heap.cpp"
KERNEL_LINKER_SCRIPT="$ROOT_DIR/kernel/boot/linker.ld"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
STAGE2_BIN="$BUILD_DIR/stage2.bin"
KERNEL_ENTRY_OBJ="$BUILD_DIR/entry64.o"
KERNEL_INTERRUPT_STUBS_OBJ="$BUILD_DIR/interrupt_stubs.o"
KERNEL_MAIN_OBJ="$BUILD_DIR/kernel_main.o"
KERNEL_PAGE_ALLOCATOR_OBJ="$BUILD_DIR/page_allocator.o"
KERNEL_PAGING_OBJ="$BUILD_DIR/paging.o"
KERNEL_RUNTIME_OBJ="$BUILD_DIR/runtime.o"
KERNEL_INTERRUPTS_OBJ="$BUILD_DIR/interrupts.o"
KERNEL_PIC_OBJ="$BUILD_DIR/pic.o"
KERNEL_PIT_OBJ="$BUILD_DIR/pit.o"
KERNEL_KEYBOARD_OBJ="$BUILD_DIR/keyboard.o"
KERNEL_HEAP_OBJ="$BUILD_DIR/heap.o"
KERNEL_ELF="$BUILD_DIR/kernel.elf"
KERNEL_BIN="$BUILD_DIR/kernel.bin"
KERNEL_META_INC="$BUILD_DIR/kernel_meta.inc"
DISK_IMG="$BUILD_DIR/disk.img"
IMAGE_SIZE=1474560
STAGE2_EXPECTED_SIZE=4096
KERNEL_START_SECTOR=10

CLANGXX_BIN="${CLANGXX_BIN:-$(command -v clang++)}"
LD_BIN="${LD_BIN:-$(command -v ld.lld)}"
OBJCOPY_BIN="${OBJCOPY_BIN:-$(command -v llvm-objcopy)}"
KERNEL_EXTRA_CXXFLAGS="${KERNEL_EXTRA_CXXFLAGS:-}"

# Build outputs live under build/ so the repo stays clean.
mkdir -p "$BUILD_DIR"

# Stage1 must remain a single 512-byte BIOS boot sector.
echo "[1/19] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# 现在内核不再只是一份入口汇编和一份 C++ 文件，
# 但整体仍然保持“没有宿主 libc、没有第三方运行时”的最小 freestanding 形态。
echo "[2/19] assembling kernel entry"
nasm -f elf64 "$KERNEL_ENTRY_SRC" -o "$KERNEL_ENTRY_OBJ"

echo "[3/19] assembling interrupt_stubs.asm"
nasm -f elf64 "$KERNEL_INTERRUPT_STUBS_SRC" -o "$KERNEL_INTERRUPT_STUBS_OBJ"

# Compile the kernel with a freestanding x86_64-elf target so the host OS ABI does not leak in.
echo "[4/19] compiling kernel_main.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_MAIN_SRC" \
  -o "$KERNEL_MAIN_OBJ"

echo "[5/19] compiling page_allocator.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_PAGE_ALLOCATOR_SRC" \
  -o "$KERNEL_PAGE_ALLOCATOR_OBJ"

echo "[6/19] compiling paging.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_PAGING_SRC" \
  -o "$KERNEL_PAGING_OBJ"

echo "[7/19] compiling runtime.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_RUNTIME_SRC" \
  -o "$KERNEL_RUNTIME_OBJ"

echo "[8/19] compiling interrupts.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_INTERRUPTS_SRC" \
  -o "$KERNEL_INTERRUPTS_OBJ"

echo "[9/19] compiling pic.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_PIC_SRC" \
  -o "$KERNEL_PIC_OBJ"

echo "[10/19] compiling pit.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_PIT_SRC" \
  -o "$KERNEL_PIT_OBJ"

echo "[11/19] compiling keyboard.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_KEYBOARD_SRC" \
  -o "$KERNEL_KEYBOARD_OBJ"

echo "[12/19] compiling heap.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
  -I "$KERNEL_INCLUDE_DIR" \
  -ffreestanding \
  -fno-exceptions \
  -fno-rtti \
  -fno-stack-protector \
  -fno-pic \
  -mno-red-zone \
  -mcmodel=kernel \
  -O0 \
  -Wall \
  -Wextra \
  $KERNEL_EXTRA_CXXFLAGS \
  -c "$KERNEL_HEAP_SRC" \
  -o "$KERNEL_HEAP_OBJ"

# Link the kernel to a fixed address. For this learning round we intentionally keep it low
# so stage2 can keep using the simplest BIOS CHS read path.
echo "[13/19] linking kernel.elf"
"$LD_BIN" \
  -m elf_x86_64 \
  -T "$KERNEL_LINKER_SCRIPT" \
  -o "$KERNEL_ELF" \
  "$KERNEL_ENTRY_OBJ" \
  "$KERNEL_INTERRUPT_STUBS_OBJ" \
  "$KERNEL_MAIN_OBJ" \
  "$KERNEL_PAGE_ALLOCATOR_OBJ" \
  "$KERNEL_PAGING_OBJ" \
  "$KERNEL_RUNTIME_OBJ" \
  "$KERNEL_INTERRUPTS_OBJ" \
  "$KERNEL_PIC_OBJ" \
  "$KERNEL_PIT_OBJ" \
  "$KERNEL_KEYBOARD_OBJ" \
  "$KERNEL_HEAP_OBJ"

# Stage2 wants a raw blob on disk, so we strip the ELF container and keep only the loadable bytes.
echo "[14/19] generating kernel.bin"
"$OBJCOPY_BIN" -O binary "$KERNEL_ELF" "$KERNEL_BIN"

kernel_size="$(wc -c < "$KERNEL_BIN" | tr -d ' ')"
kernel_sectors="$(((kernel_size + 511) / 512))"

if [ "$kernel_sectors" -le 0 ] || [ "$kernel_sectors" -gt 127 ]; then
  echo "kernel.bin must occupy between 1 and 127 sectors in this CHS-only round, got $kernel_sectors sectors" >&2
  exit 1
fi

# Stage2 needs to know how many sectors to read and what fixed address the kernel expects.
echo "[15/19] generating kernel metadata for stage2"
cat > "$KERNEL_META_INC" <<EOF
%define KERNEL_LOAD_ADDR 0x00010000
%define KERNEL_LOAD_SEGMENT 0x1000
%define KERNEL_LOAD_OFFSET 0x0000
%define KERNEL_START_SECTOR $KERNEL_START_SECTOR
%define KERNEL_SECTORS $kernel_sectors
EOF

# Stage2 now spans eight sectors because it also sets up A20/E820/GDT/page tables/long mode.
echo "[16/19] assembling stage2"
nasm -f bin -i "$BUILD_DIR/" "$STAGE2_SRC" -o "$STAGE2_BIN"

size="$(wc -c < "$STAGE2_BIN" | tr -d ' ')"
if [ "$size" -ne "$STAGE2_EXPECTED_SIZE" ]; then
  echo "stage2 size must be exactly $STAGE2_EXPECTED_SIZE bytes, got $size" >&2
  exit 1
fi

# Create a raw floppy-sized image and place stage1/stage2/kernel in the first sectors.
echo "[17/19] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
dd if="$STAGE2_BIN" of="$DISK_IMG" bs=512 seek=1 count=8 conv=notrunc status=none
dd if="$KERNEL_BIN" of="$DISK_IMG" bs=512 seek=9 conv=notrunc status=none

# BIOS boot sectors must end with the 0x55aa signature.
echo "[18/19] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

# Emit the key output paths so manual runs can inspect the artifacts directly.
echo "[19/19] build complete"
echo "stage1.bin: $STAGE1_BIN"
echo "stage2.bin: $STAGE2_BIN"
echo "kernel.elf: $KERNEL_ELF"
echo "kernel.bin: $KERNEL_BIN"
echo "kernel sectors: $kernel_sectors"
echo "disk.img:    $DISK_IMG"
echo "signature:   $signature"
