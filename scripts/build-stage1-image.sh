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
KERNEL_CONSOLE_SRC="$ROOT_DIR/kernel/console/console.cpp"
KERNEL_SHELL_SRC="$ROOT_DIR/kernel/shell/shell.cpp"
KERNEL_PAGE_ALLOCATOR_SRC="$ROOT_DIR/kernel/memory/page_allocator.cpp"
KERNEL_PAGING_SRC="$ROOT_DIR/kernel/memory/paging.cpp"
KERNEL_RUNTIME_SRC="$ROOT_DIR/kernel/runtime/runtime.cpp"
KERNEL_INTERRUPTS_SRC="$ROOT_DIR/kernel/interrupts/interrupts.cpp"
KERNEL_PIC_SRC="$ROOT_DIR/kernel/interrupts/pic.cpp"
KERNEL_PIT_SRC="$ROOT_DIR/kernel/interrupts/pit.cpp"
KERNEL_KEYBOARD_SRC="$ROOT_DIR/kernel/interrupts/keyboard.cpp"
KERNEL_HEAP_SRC="$ROOT_DIR/kernel/memory/heap.cpp"
KERNEL_KMEMORY_SRC="$ROOT_DIR/kernel/memory/kmemory.cpp"
KERNEL_BOOT_VOLUME_SRC="$ROOT_DIR/kernel/storage/boot_volume.cpp"
KERNEL_LINKER_SCRIPT="$ROOT_DIR/kernel/boot/linker.ld"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
STAGE2_BIN="$BUILD_DIR/stage2.bin"
KERNEL_ENTRY_OBJ="$BUILD_DIR/entry64.o"
KERNEL_INTERRUPT_STUBS_OBJ="$BUILD_DIR/interrupt_stubs.o"
KERNEL_MAIN_OBJ="$BUILD_DIR/kernel_main.o"
KERNEL_CONSOLE_OBJ="$BUILD_DIR/console.o"
KERNEL_SHELL_OBJ="$BUILD_DIR/shell.o"
KERNEL_PAGE_ALLOCATOR_OBJ="$BUILD_DIR/page_allocator.o"
KERNEL_PAGING_OBJ="$BUILD_DIR/paging.o"
KERNEL_RUNTIME_OBJ="$BUILD_DIR/runtime.o"
KERNEL_INTERRUPTS_OBJ="$BUILD_DIR/interrupts.o"
KERNEL_PIC_OBJ="$BUILD_DIR/pic.o"
KERNEL_PIT_OBJ="$BUILD_DIR/pit.o"
KERNEL_KEYBOARD_OBJ="$BUILD_DIR/keyboard.o"
KERNEL_HEAP_OBJ="$BUILD_DIR/heap.o"
KERNEL_KMEMORY_OBJ="$BUILD_DIR/kmemory.o"
KERNEL_BOOT_VOLUME_OBJ="$BUILD_DIR/boot_volume.o"
KERNEL_ELF="$BUILD_DIR/kernel.elf"
KERNEL_BIN="$BUILD_DIR/kernel.bin"
BOOT_VOLUME_BIN="$BUILD_DIR/boot_volume.bin"
KERNEL_META_INC="$BUILD_DIR/kernel_meta.inc"
DISK_IMG="$BUILD_DIR/disk.img"
IMAGE_SIZE=1474560
STAGE2_EXPECTED_SIZE=4096
KERNEL_START_SECTOR=10
BOOT_VOLUME_SECTORS=4
BOOT_VOLUME_LOAD_ADDR=0x00020000
BOOT_VOLUME_LOAD_SEGMENT=0x2000
BOOT_VOLUME_LOAD_OFFSET=0x0000
BOOT_VOLUME_BYTES=$((BOOT_VOLUME_SECTORS * 512))
BOOT_VOLUME_NAME="boot-volume"
BOOT_VOLUME_README="boot volume sector 1: hello from os64"
BOOT_VOLUME_NOTES="boot volume sector 2: next step is filesystem"

CLANGXX_BIN="${CLANGXX_BIN:-$(command -v clang++)}"
LD_BIN="${LD_BIN:-$(command -v ld.lld)}"
OBJCOPY_BIN="${OBJCOPY_BIN:-$(command -v llvm-objcopy)}"
KERNEL_EXTRA_CXXFLAGS="${KERNEL_EXTRA_CXXFLAGS:-}"

# Build outputs live under build/ so the repo stays clean.
mkdir -p "$BUILD_DIR"

write_le32() {
  local value="$1"
  printf '%b' \
    "\\$(printf '%03o' $(( value        & 0xff )))\
\\$(printf '%03o' $(((value >> 8)  & 0xff)))\
\\$(printf '%03o' $(((value >> 16) & 0xff)))\
\\$(printf '%03o' $(((value >> 24) & 0xff)))"
}

# Stage1 must remain a single 512-byte BIOS boot sector.
echo "[1/24] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# 现在内核不再只是一份入口汇编和一份 C++ 文件，
# 但整体仍然保持“没有宿主 libc、没有第三方运行时”的最小 freestanding 形态。
echo "[2/24] assembling kernel entry"
nasm -f elf64 "$KERNEL_ENTRY_SRC" -o "$KERNEL_ENTRY_OBJ"

echo "[3/24] assembling interrupt_stubs.asm"
nasm -f elf64 "$KERNEL_INTERRUPT_STUBS_SRC" -o "$KERNEL_INTERRUPT_STUBS_OBJ"

# Compile the kernel with a freestanding x86_64-elf target so the host OS ABI does not leak in.
echo "[4/24] compiling kernel_main.cpp"
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

echo "[5/24] compiling console.cpp"
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
  -c "$KERNEL_CONSOLE_SRC" \
  -o "$KERNEL_CONSOLE_OBJ"

echo "[6/24] compiling shell.cpp"
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
  -c "$KERNEL_SHELL_SRC" \
  -o "$KERNEL_SHELL_OBJ"

echo "[7/24] compiling page_allocator.cpp"
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

echo "[8/24] compiling paging.cpp"
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

echo "[9/24] compiling runtime.cpp"
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

echo "[10/24] compiling interrupts.cpp"
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

echo "[11/24] compiling pic.cpp"
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

echo "[12/24] compiling pit.cpp"
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

echo "[13/24] compiling keyboard.cpp"
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

echo "[14/24] compiling heap.cpp"
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

echo "[15/24] compiling kmemory.cpp"
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
  -c "$KERNEL_KMEMORY_SRC" \
  -o "$KERNEL_KMEMORY_OBJ"

echo "[16/24] compiling boot_volume.cpp"
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
  -c "$KERNEL_BOOT_VOLUME_SRC" \
  -o "$KERNEL_BOOT_VOLUME_OBJ"

# Link the kernel to a fixed address. For this learning round we intentionally keep it low
# so stage2 can keep using the simplest BIOS CHS read path.
echo "[17/24] linking kernel.elf"
"$LD_BIN" \
  -m elf_x86_64 \
  -T "$KERNEL_LINKER_SCRIPT" \
  -o "$KERNEL_ELF" \
  "$KERNEL_ENTRY_OBJ" \
  "$KERNEL_INTERRUPT_STUBS_OBJ" \
  "$KERNEL_MAIN_OBJ" \
  "$KERNEL_CONSOLE_OBJ" \
  "$KERNEL_SHELL_OBJ" \
  "$KERNEL_PAGE_ALLOCATOR_OBJ" \
  "$KERNEL_PAGING_OBJ" \
  "$KERNEL_RUNTIME_OBJ" \
  "$KERNEL_INTERRUPTS_OBJ" \
  "$KERNEL_PIC_OBJ" \
  "$KERNEL_PIT_OBJ" \
  "$KERNEL_KEYBOARD_OBJ" \
  "$KERNEL_HEAP_OBJ" \
  "$KERNEL_KMEMORY_OBJ" \
  "$KERNEL_BOOT_VOLUME_OBJ"

# Stage2 wants a raw blob on disk, so we strip the ELF container and keep only the loadable bytes.
echo "[18/24] generating kernel.bin"
"$OBJCOPY_BIN" -O binary "$KERNEL_ELF" "$KERNEL_BIN"

kernel_size="$(wc -c < "$KERNEL_BIN" | tr -d ' ')"
kernel_sectors="$(((kernel_size + 511) / 512))"
boot_volume_start_sector="$((KERNEL_START_SECTOR + kernel_sectors))"
boot_volume_readme_length="${#BOOT_VOLUME_README}"
boot_volume_notes_length="${#BOOT_VOLUME_NOTES}"

if [ "$kernel_sectors" -le 0 ] || [ "$kernel_sectors" -gt 127 ]; then
  echo "kernel.bin must occupy between 1 and 127 sectors in this CHS-only round, got $kernel_sectors sectors" >&2
  exit 1
fi

echo "[19/24] generating boot_volume.bin"
truncate -s "$BOOT_VOLUME_BYTES" "$BOOT_VOLUME_BIN"
printf 'OS64VOL1' | dd of="$BOOT_VOLUME_BIN" bs=1 seek=0 conv=notrunc status=none
write_le32 1 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=8 conv=notrunc status=none
write_le32 "$BOOT_VOLUME_SECTORS" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=12 conv=notrunc status=none
write_le32 512 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=16 conv=notrunc status=none
printf '%s' "$BOOT_VOLUME_NAME" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=20 conv=notrunc status=none
write_le32 1 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=36 conv=notrunc status=none
write_le32 "$boot_volume_readme_length" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=40 conv=notrunc status=none
write_le32 2 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=44 conv=notrunc status=none
write_le32 "$boot_volume_notes_length" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=48 conv=notrunc status=none
printf '%s' "$BOOT_VOLUME_README" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=512 conv=notrunc status=none
printf '%s' "$BOOT_VOLUME_NOTES" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=1024 conv=notrunc status=none

# Stage2 needs to know how many sectors to read and what fixed address the kernel expects.
echo "[20/24] generating kernel metadata for stage2"
cat > "$KERNEL_META_INC" <<EOF
%define KERNEL_LOAD_ADDR 0x00010000
%define KERNEL_LOAD_SEGMENT 0x1000
%define KERNEL_LOAD_OFFSET 0x0000
%define KERNEL_START_SECTOR $KERNEL_START_SECTOR
%define KERNEL_SECTORS $kernel_sectors
%define BOOT_VOLUME_LOAD_ADDR $BOOT_VOLUME_LOAD_ADDR
%define BOOT_VOLUME_LOAD_SEGMENT $BOOT_VOLUME_LOAD_SEGMENT
%define BOOT_VOLUME_LOAD_OFFSET $BOOT_VOLUME_LOAD_OFFSET
%define BOOT_VOLUME_START_SECTOR $boot_volume_start_sector
%define BOOT_VOLUME_SECTORS $BOOT_VOLUME_SECTORS
EOF

# Stage2 now spans eight sectors because it also sets up A20/E820/GDT/page tables/long mode.
echo "[21/24] assembling stage2"
nasm -f bin -i "$BUILD_DIR/" "$STAGE2_SRC" -o "$STAGE2_BIN"

size="$(wc -c < "$STAGE2_BIN" | tr -d ' ')"
if [ "$size" -ne "$STAGE2_EXPECTED_SIZE" ]; then
  echo "stage2 size must be exactly $STAGE2_EXPECTED_SIZE bytes, got $size" >&2
  exit 1
fi

# Create a raw floppy-sized image and place stage1/stage2/kernel in the first sectors.
echo "[22/24] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
dd if="$STAGE2_BIN" of="$DISK_IMG" bs=512 seek=1 count=8 conv=notrunc status=none
dd if="$KERNEL_BIN" of="$DISK_IMG" bs=512 seek=9 conv=notrunc status=none
dd if="$BOOT_VOLUME_BIN" of="$DISK_IMG" bs=512 seek=$((boot_volume_start_sector - 1)) conv=notrunc status=none

# BIOS boot sectors must end with the 0x55aa signature.
echo "[23/24] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

# Emit the key output paths so manual runs can inspect the artifacts directly.
echo "[24/24] build complete"
echo "stage1.bin: $STAGE1_BIN"
echo "stage2.bin: $STAGE2_BIN"
echo "kernel.elf: $KERNEL_ELF"
echo "kernel.bin: $KERNEL_BIN"
echo "boot_volume.bin: $BOOT_VOLUME_BIN"
echo "kernel sectors: $kernel_sectors"
echo "boot volume sectors: $BOOT_VOLUME_SECTORS"
echo "boot volume start sector: $boot_volume_start_sector"
echo "disk.img:    $DISK_IMG"
echo "signature:   $signature"
