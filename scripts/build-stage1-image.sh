#!/usr/bin/env bash
set -euo pipefail

# Resolve the repo root once so the script works from any current directory.
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
STAGE1_SRC="$ROOT_DIR/boot/stage1.asm"
STAGE2_SRC="$ROOT_DIR/boot/stage2.asm"
KERNEL_ENTRY_SRC="$ROOT_DIR/kernel/entry64.asm"
KERNEL_MAIN_SRC="$ROOT_DIR/kernel/kernel_main.cpp"
KERNEL_PAGE_ALLOCATOR_SRC="$ROOT_DIR/kernel/page_allocator.cpp"
KERNEL_PAGING_SRC="$ROOT_DIR/kernel/paging.cpp"
KERNEL_RUNTIME_SRC="$ROOT_DIR/kernel/runtime.cpp"
KERNEL_LINKER_SCRIPT="$ROOT_DIR/kernel/linker.ld"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
STAGE2_BIN="$BUILD_DIR/stage2.bin"
KERNEL_ENTRY_OBJ="$BUILD_DIR/entry64.o"
KERNEL_MAIN_OBJ="$BUILD_DIR/kernel_main.o"
KERNEL_PAGE_ALLOCATOR_OBJ="$BUILD_DIR/page_allocator.o"
KERNEL_PAGING_OBJ="$BUILD_DIR/paging.o"
KERNEL_RUNTIME_OBJ="$BUILD_DIR/runtime.o"
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

# Build outputs live under build/ so the repo stays clean.
mkdir -p "$BUILD_DIR"

# Stage1 must remain a single 512-byte BIOS boot sector.
echo "[1/11] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# The 64-bit kernel still stays intentionally tiny in this round:
# one assembly entry file + one freestanding C++ file.
echo "[2/11] assembling kernel entry"
nasm -f elf64 "$KERNEL_ENTRY_SRC" -o "$KERNEL_ENTRY_OBJ"

# Compile the kernel with a freestanding x86_64-elf target so the host OS ABI does not leak in.
echo "[3/11] compiling kernel_main.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
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
  -c "$KERNEL_MAIN_SRC" \
  -o "$KERNEL_MAIN_OBJ"

echo "[4/11] compiling page_allocator.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
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
  -c "$KERNEL_PAGE_ALLOCATOR_SRC" \
  -o "$KERNEL_PAGE_ALLOCATOR_OBJ"

echo "[5/13] compiling paging.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
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
  -c "$KERNEL_PAGING_SRC" \
  -o "$KERNEL_PAGING_OBJ"

echo "[6/13] compiling runtime.cpp"
"$CLANGXX_BIN" \
  --target=x86_64-elf \
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
  -c "$KERNEL_RUNTIME_SRC" \
  -o "$KERNEL_RUNTIME_OBJ"

# Link the kernel to a fixed address. For this learning round we intentionally keep it low
# so stage2 can keep using the simplest BIOS CHS read path.
echo "[7/13] linking kernel.elf"
"$LD_BIN" \
  -m elf_x86_64 \
  -T "$KERNEL_LINKER_SCRIPT" \
  -o "$KERNEL_ELF" \
  "$KERNEL_ENTRY_OBJ" \
  "$KERNEL_MAIN_OBJ" \
  "$KERNEL_PAGE_ALLOCATOR_OBJ" \
  "$KERNEL_PAGING_OBJ" \
  "$KERNEL_RUNTIME_OBJ"

# Stage2 wants a raw blob on disk, so we strip the ELF container and keep only the loadable bytes.
echo "[8/13] generating kernel.bin"
"$OBJCOPY_BIN" -O binary "$KERNEL_ELF" "$KERNEL_BIN"

kernel_size="$(wc -c < "$KERNEL_BIN" | tr -d ' ')"
kernel_sectors="$(((kernel_size + 511) / 512))"

if [ "$kernel_sectors" -le 0 ] || [ "$kernel_sectors" -gt 127 ]; then
  echo "kernel.bin must occupy between 1 and 127 sectors in this CHS-only round, got $kernel_sectors sectors" >&2
  exit 1
fi

# Stage2 needs to know how many sectors to read and what fixed address the kernel expects.
echo "[9/13] generating kernel metadata for stage2"
cat > "$KERNEL_META_INC" <<EOF
%define KERNEL_LOAD_ADDR 0x00010000
%define KERNEL_LOAD_SEGMENT 0x1000
%define KERNEL_LOAD_OFFSET 0x0000
%define KERNEL_START_SECTOR $KERNEL_START_SECTOR
%define KERNEL_SECTORS $kernel_sectors
EOF

# Stage2 now spans eight sectors because it also sets up A20/E820/GDT/page tables/long mode.
echo "[10/13] assembling stage2"
nasm -f bin -i "$BUILD_DIR/" "$STAGE2_SRC" -o "$STAGE2_BIN"

size="$(wc -c < "$STAGE2_BIN" | tr -d ' ')"
if [ "$size" -ne "$STAGE2_EXPECTED_SIZE" ]; then
  echo "stage2 size must be exactly $STAGE2_EXPECTED_SIZE bytes, got $size" >&2
  exit 1
fi

# Create a raw floppy-sized image and place stage1/stage2/kernel in the first sectors.
echo "[11/13] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
dd if="$STAGE2_BIN" of="$DISK_IMG" bs=512 seek=1 count=8 conv=notrunc status=none
dd if="$KERNEL_BIN" of="$DISK_IMG" bs=512 seek=9 conv=notrunc status=none

# BIOS boot sectors must end with the 0x55aa signature.
echo "[12/13] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

# Emit the key output paths so manual runs can inspect the artifacts directly.
echo "[13/13] build complete"
echo "stage1.bin: $STAGE1_BIN"
echo "stage2.bin: $STAGE2_BIN"
echo "kernel.elf: $KERNEL_ELF"
echo "kernel.bin: $KERNEL_BIN"
echo "kernel sectors: $kernel_sectors"
echo "disk.img:    $DISK_IMG"
echo "signature:   $signature"
