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
KERNEL_TASK_CONTEXT_SRC="$ROOT_DIR/kernel/task/context_switch.asm"
USER_HELLO_SRC="$ROOT_DIR/user/hello.asm"
USER_HELLO_ELF_SRC="$ROOT_DIR/user/hello_elf.asm"
USER_HELLO_ELF_LINKER_SCRIPT="$ROOT_DIR/user/hello_elf.ld"
KERNEL_MAIN_SRC="$ROOT_DIR/kernel/core/kernel_main.cpp"
KERNEL_CONSOLE_SRC="$ROOT_DIR/kernel/console/console.cpp"
KERNEL_SHELL_SRC="$ROOT_DIR/kernel/shell/shell.cpp"
KERNEL_PAGE_ALLOCATOR_SRC="$ROOT_DIR/kernel/memory/page_allocator.cpp"
KERNEL_PAGING_SRC="$ROOT_DIR/kernel/memory/paging.cpp"
KERNEL_ADDRESS_SPACE_SRC="$ROOT_DIR/kernel/memory/address_space.cpp"
KERNEL_RUNTIME_SRC="$ROOT_DIR/kernel/runtime/runtime.cpp"
KERNEL_INTERRUPTS_SRC="$ROOT_DIR/kernel/interrupts/interrupts.cpp"
KERNEL_PIC_SRC="$ROOT_DIR/kernel/interrupts/pic.cpp"
KERNEL_PIT_SRC="$ROOT_DIR/kernel/interrupts/pit.cpp"
KERNEL_KEYBOARD_SRC="$ROOT_DIR/kernel/interrupts/keyboard.cpp"
KERNEL_HEAP_SRC="$ROOT_DIR/kernel/memory/heap.cpp"
KERNEL_KMEMORY_SRC="$ROOT_DIR/kernel/memory/kmemory.cpp"
KERNEL_BOOT_VOLUME_SRC="$ROOT_DIR/kernel/storage/boot_volume.cpp"
KERNEL_BLOCK_DEVICE_SRC="$ROOT_DIR/kernel/storage/block_device.cpp"
KERNEL_OS64FS_SRC="$ROOT_DIR/kernel/fs/os64fs.cpp"
KERNEL_FILE_SRC="$ROOT_DIR/kernel/fs/file.cpp"
KERNEL_DIRECTORY_SRC="$ROOT_DIR/kernel/fs/directory.cpp"
KERNEL_VFS_SRC="$ROOT_DIR/kernel/fs/vfs.cpp"
KERNEL_FD_SRC="$ROOT_DIR/kernel/fs/fd.cpp"
KERNEL_SYSCALL_SRC="$ROOT_DIR/kernel/syscall/syscall.cpp"
KERNEL_ELF_LOADER_SRC="$ROOT_DIR/kernel/task/elf_loader.cpp"
KERNEL_SCHEDULER_SRC="$ROOT_DIR/kernel/task/scheduler.cpp"
KERNEL_LINKER_SCRIPT="$ROOT_DIR/kernel/boot/linker.ld"
STAGE1_BIN="$BUILD_DIR/stage1.bin"
STAGE2_BIN="$BUILD_DIR/stage2.bin"
KERNEL_ENTRY_OBJ="$BUILD_DIR/entry64.o"
KERNEL_INTERRUPT_STUBS_OBJ="$BUILD_DIR/interrupt_stubs.o"
KERNEL_TASK_CONTEXT_OBJ="$BUILD_DIR/context_switch.o"
USER_HELLO_BIN="$BUILD_DIR/hello.bin"
USER_HELLO_ELF_OBJ="$BUILD_DIR/hello_elf.o"
USER_HELLO_ELF_UNSTRIPPED="$BUILD_DIR/hello.unstripped.elf"
USER_HELLO_ELF_BIN="$BUILD_DIR/hello.elf"
KERNEL_MAIN_OBJ="$BUILD_DIR/kernel_main.o"
KERNEL_CONSOLE_OBJ="$BUILD_DIR/console.o"
KERNEL_SHELL_OBJ="$BUILD_DIR/shell.o"
KERNEL_PAGE_ALLOCATOR_OBJ="$BUILD_DIR/page_allocator.o"
KERNEL_PAGING_OBJ="$BUILD_DIR/paging.o"
KERNEL_ADDRESS_SPACE_OBJ="$BUILD_DIR/address_space.o"
KERNEL_RUNTIME_OBJ="$BUILD_DIR/runtime.o"
KERNEL_INTERRUPTS_OBJ="$BUILD_DIR/interrupts.o"
KERNEL_PIC_OBJ="$BUILD_DIR/pic.o"
KERNEL_PIT_OBJ="$BUILD_DIR/pit.o"
KERNEL_KEYBOARD_OBJ="$BUILD_DIR/keyboard.o"
KERNEL_HEAP_OBJ="$BUILD_DIR/heap.o"
KERNEL_KMEMORY_OBJ="$BUILD_DIR/kmemory.o"
KERNEL_BOOT_VOLUME_OBJ="$BUILD_DIR/boot_volume.o"
KERNEL_BLOCK_DEVICE_OBJ="$BUILD_DIR/block_device.o"
KERNEL_OS64FS_OBJ="$BUILD_DIR/os64fs.o"
KERNEL_FILE_OBJ="$BUILD_DIR/file.o"
KERNEL_DIRECTORY_OBJ="$BUILD_DIR/directory.o"
KERNEL_VFS_OBJ="$BUILD_DIR/vfs.o"
KERNEL_FD_OBJ="$BUILD_DIR/fd.o"
KERNEL_SYSCALL_OBJ="$BUILD_DIR/syscall.o"
KERNEL_ELF_LOADER_OBJ="$BUILD_DIR/elf_loader.o"
KERNEL_SCHEDULER_OBJ="$BUILD_DIR/scheduler.o"
KERNEL_ELF="$BUILD_DIR/kernel.elf"
KERNEL_BIN="$BUILD_DIR/kernel.bin"
BOOT_VOLUME_BIN="$BUILD_DIR/boot_volume.bin"
KERNEL_META_INC="$BUILD_DIR/kernel_meta.inc"
DISK_IMG="$BUILD_DIR/disk.img"
IMAGE_SIZE=1474560
STAGE2_EXPECTED_SIZE=4096
KERNEL_START_SECTOR=10
BOOT_VOLUME_SECTORS=128
# Keep the preloaded boot volume away from the kernel image plus .bss.
# It still stays below 2 MiB, so the early identity mapping can access it.
BOOT_VOLUME_LOAD_ADDR=0x00080000
BOOT_VOLUME_LOAD_SEGMENT=0x8000
BOOT_VOLUME_LOAD_OFFSET=0x0000
BOOT_VOLUME_BYTES=$((BOOT_VOLUME_SECTORS * 512))
OS64FS_VOLUME_NAME="os64-root"
OS64FS_SIGNATURE="OS64FSV3"
OS64FS_INODE_COUNT=32
OS64FS_INODE_SIZE=64
OS64FS_INODE_BITMAP_START_SECTOR=1
OS64FS_INODE_BITMAP_SECTOR_COUNT=1
OS64FS_DATA_BITMAP_START_SECTOR=2
OS64FS_DATA_BITMAP_SECTOR_COUNT=1
OS64FS_INODE_TABLE_START_SECTOR=3
OS64FS_INODE_TABLE_SECTOR_COUNT=4
OS64FS_DATA_START_SECTOR=7
OS64FS_DATA_SECTOR_COUNT=121
OS64FS_DATA_BLOCK_SIZE=512
OS64FS_DIRECTORY_ENTRY_SIZE=64
OS64FS_ROOT_INODE=1
OS64FS_INVALID_BLOCK=4294967295
OS64FS_BIGFILE_NAME="big.txt"
OS64FS_BIGFILE_BLOCK_COUNT=10
OS64FS_README="os64fs readme: the 64-bit kernel now mounts a real read-only filesystem."
OS64FS_NOTES="os64fs notes: next steps are write support, cache, and real disk drivers."
OS64FS_GUIDE="os64fs guide: stage2 only preloads raw sectors. the block device layer turns that memory into sector reads, and the filesystem layer resolves paths like docs/guide.txt inside the 64-bit kernel."

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

write_le16() {
  local value="$1"
  printf '%b' \
    "\\$(printf '%03o' $(( value       & 0xff )))\
\\$(printf '%03o' $(((value >> 8) & 0xff)))"
}

write_u8() {
  local value="$1"
  printf '%b' "\\$(printf '%03o' $(( value & 0xff )))"
}

write_bitmap_bit() {
  local base_offset="$1"
  local bit_index="$2"
  local byte_offset=$((base_offset + bit_index / 8))
  local bit_mask=$((1 << (bit_index % 8)))
  local current_byte

  current_byte="$(od -An -tu1 -N1 -j "$byte_offset" "$BOOT_VOLUME_BIN" | tr -d '[:space:]')"
  if [ -z "$current_byte" ]; then
    current_byte=0
  fi

  write_u8 $((current_byte | bit_mask)) | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$byte_offset" conv=notrunc status=none
}

write_inode() {
  local base_offset="$1"
  local inode_number="$2"
  local type="$3"
  local link_count="$4"
  local size_bytes="$5"
  local mode="$6"
  local block_count="$7"
  local indirect_block="$8"

  write_le32 "$inode_number" | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$base_offset" conv=notrunc status=none
  write_le16 "$type" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 4)) conv=notrunc status=none
  write_le16 "$link_count" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 6)) conv=notrunc status=none
  write_le32 "$size_bytes" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 8)) conv=notrunc status=none
  write_le32 "$mode" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 12)) conv=notrunc status=none

  for block_slot in 0 1 2 3 4 5 6 7; do
    write_le32 "$OS64FS_INVALID_BLOCK" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 16 + block_slot * 4)) conv=notrunc status=none
  done

  write_le32 "$indirect_block" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 48)) conv=notrunc status=none
  write_le32 "$block_count" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 52)) conv=notrunc status=none
  write_le32 0 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 56)) conv=notrunc status=none
  write_le32 0 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 60)) conv=notrunc status=none
}

write_inode_direct_block() {
  local base_offset="$1"
  local block_slot="$2"
  local block_index="$3"

  write_le32 "$block_index" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 16 + block_slot * 4)) conv=notrunc status=none
}

write_dir_entry() {
  local base_offset="$1"
  local inode_number="$2"
  local type="$3"
  local name="$4"
  local name_length="${#name}"

  write_le32 "$inode_number" | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$base_offset" conv=notrunc status=none
  write_le16 "$type" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 4)) conv=notrunc status=none
  write_u8 "$name_length" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 6)) conv=notrunc status=none
  write_u8 0 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 7)) conv=notrunc status=none
  printf '%s' "$name" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((base_offset + 8)) conv=notrunc status=none
}

# Stage1 must remain a single 512-byte BIOS boot sector.
echo "[1/34] assembling stage1"
nasm -f bin "$STAGE1_SRC" -o "$STAGE1_BIN"

size="$(wc -c < "$STAGE1_BIN" | tr -d ' ')"
if [ "$size" -ne 512 ]; then
  echo "stage1 size must be exactly 512 bytes, got $size" >&2
  exit 1
fi

# 现在内核不再只是一份入口汇编和一份 C++ 文件，
# 但整体仍然保持“没有宿主 libc、没有第三方运行时”的最小 freestanding 形态。
echo "[2/34] assembling kernel entry"
nasm -f elf64 "$KERNEL_ENTRY_SRC" -o "$KERNEL_ENTRY_OBJ"

echo "[3/34] assembling interrupt_stubs.asm"
nasm -f elf64 "$KERNEL_INTERRUPT_STUBS_SRC" -o "$KERNEL_INTERRUPT_STUBS_OBJ"

echo "[4/34] assembling context_switch.asm"
nasm -f elf64 "$KERNEL_TASK_CONTEXT_SRC" -o "$KERNEL_TASK_CONTEXT_OBJ"

echo "[extra] assembling user/hello.asm"
nasm -f bin "$USER_HELLO_SRC" -o "$USER_HELLO_BIN"

echo "[extra] assembling user/hello_elf.asm"
nasm -f elf64 "$USER_HELLO_ELF_SRC" -o "$USER_HELLO_ELF_OBJ"

echo "[extra] linking user/hello.elf"
"$LD_BIN" \
  -m elf_x86_64 \
  -nostdlib \
  -z max-page-size=0x8 \
  -T "$USER_HELLO_ELF_LINKER_SCRIPT" \
  -o "$USER_HELLO_ELF_UNSTRIPPED" \
  "$USER_HELLO_ELF_OBJ"

echo "[extra] stripping user/hello.elf"
"$OBJCOPY_BIN" --strip-all "$USER_HELLO_ELF_UNSTRIPPED" "$USER_HELLO_ELF_BIN"

# Compile the kernel with a freestanding x86_64-elf target so the host OS ABI does not leak in.
echo "[5/34] compiling kernel_main.cpp"
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

echo "[6/34] compiling console.cpp"
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

echo "[7/34] compiling shell.cpp"
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

echo "[8/34] compiling page_allocator.cpp"
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

echo "[9/34] compiling paging.cpp"
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

echo "[10/34] compiling address_space.cpp"
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
  -c "$KERNEL_ADDRESS_SPACE_SRC" \
  -o "$KERNEL_ADDRESS_SPACE_OBJ"

echo "[11/34] compiling runtime.cpp"
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

echo "[12/34] compiling interrupts.cpp"
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

echo "[13/34] compiling pic.cpp"
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

echo "[14/34] compiling pit.cpp"
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

echo "[15/34] compiling keyboard.cpp"
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

echo "[16/34] compiling heap.cpp"
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

echo "[17/34] compiling kmemory.cpp"
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

echo "[18/34] compiling boot_volume.cpp"
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

echo "[19/34] compiling block_device.cpp"
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
  -c "$KERNEL_BLOCK_DEVICE_SRC" \
  -o "$KERNEL_BLOCK_DEVICE_OBJ"

echo "[20/34] compiling os64fs.cpp"
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
  -c "$KERNEL_OS64FS_SRC" \
  -o "$KERNEL_OS64FS_OBJ"

echo "[21/34] compiling file.cpp"
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
  -c "$KERNEL_FILE_SRC" \
  -o "$KERNEL_FILE_OBJ"

echo "[22/34] compiling directory.cpp"
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
  -c "$KERNEL_DIRECTORY_SRC" \
  -o "$KERNEL_DIRECTORY_OBJ"

echo "[23/34] compiling vfs.cpp"
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
  -c "$KERNEL_VFS_SRC" \
  -o "$KERNEL_VFS_OBJ"

echo "[24/34] compiling fd.cpp"
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
  -c "$KERNEL_FD_SRC" \
  -o "$KERNEL_FD_OBJ"

echo "[25/34] compiling syscall.cpp"
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
  -c "$KERNEL_SYSCALL_SRC" \
  -o "$KERNEL_SYSCALL_OBJ"

echo "[26/34] compiling scheduler.cpp"
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
  -c "$KERNEL_SCHEDULER_SRC" \
  -o "$KERNEL_SCHEDULER_OBJ"

echo "[extra] compiling elf_loader.cpp"
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
  -c "$KERNEL_ELF_LOADER_SRC" \
  -o "$KERNEL_ELF_LOADER_OBJ"

# Link the kernel to a fixed address. For this learning round we intentionally keep it low
# so stage2 can keep using the simplest BIOS CHS read path.
echo "[27/34] linking kernel.elf"
"$LD_BIN" \
  -m elf_x86_64 \
  -T "$KERNEL_LINKER_SCRIPT" \
  -o "$KERNEL_ELF" \
  "$KERNEL_ENTRY_OBJ" \
  "$KERNEL_INTERRUPT_STUBS_OBJ" \
  "$KERNEL_TASK_CONTEXT_OBJ" \
  "$KERNEL_MAIN_OBJ" \
  "$KERNEL_CONSOLE_OBJ" \
  "$KERNEL_SHELL_OBJ" \
  "$KERNEL_PAGE_ALLOCATOR_OBJ" \
  "$KERNEL_PAGING_OBJ" \
  "$KERNEL_ADDRESS_SPACE_OBJ" \
  "$KERNEL_RUNTIME_OBJ" \
  "$KERNEL_INTERRUPTS_OBJ" \
  "$KERNEL_PIC_OBJ" \
  "$KERNEL_PIT_OBJ" \
  "$KERNEL_KEYBOARD_OBJ" \
  "$KERNEL_HEAP_OBJ" \
  "$KERNEL_KMEMORY_OBJ" \
  "$KERNEL_BOOT_VOLUME_OBJ" \
  "$KERNEL_BLOCK_DEVICE_OBJ" \
  "$KERNEL_OS64FS_OBJ" \
  "$KERNEL_FILE_OBJ" \
  "$KERNEL_DIRECTORY_OBJ" \
  "$KERNEL_VFS_OBJ" \
  "$KERNEL_FD_OBJ" \
  "$KERNEL_SYSCALL_OBJ" \
  "$KERNEL_ELF_LOADER_OBJ" \
  "$KERNEL_SCHEDULER_OBJ"

# Stage2 wants a raw blob on disk, so we strip the ELF container and keep only the loadable bytes.
echo "[28/34] generating kernel.bin"
"$OBJCOPY_BIN" -O binary "$KERNEL_ELF" "$KERNEL_BIN"

kernel_size="$(wc -c < "$KERNEL_BIN" | tr -d ' ')"
kernel_sectors="$(((kernel_size + 511) / 512))"
boot_volume_start_sector="$((KERNEL_START_SECTOR + kernel_sectors))"
image_total_sectors="$((IMAGE_SIZE / 512))"
max_kernel_sectors="$((image_total_sectors - (KERNEL_START_SECTOR - 1) - BOOT_VOLUME_SECTORS))"
os64fs_readme_length="${#OS64FS_README}"
os64fs_notes_length="${#OS64FS_NOTES}"
os64fs_guide_length="${#OS64FS_GUIDE}"
os64fs_hello_length="$(wc -c < "$USER_HELLO_BIN" | tr -d ' ')"
os64fs_hello_elf_length="$(wc -c < "$USER_HELLO_ELF_BIN" | tr -d ' ')"
os64fs_bigfile_tmp="$BUILD_DIR/os64fs_big.bin"
truncate -s 0 "$os64fs_bigfile_tmp"
for block_index in 0 1 2 3 4 5 6 7 8 9; do
  block_char="$(printf "\\$(printf '%03o' $((65 + block_index)))")"
  printf '%*s' "$OS64FS_DATA_BLOCK_SIZE" '' | tr ' ' "$block_char" >> "$os64fs_bigfile_tmp"
done
os64fs_bigfile_length="$(wc -c < "$os64fs_bigfile_tmp" | tr -d ' ')"
os64fs_hello_elf_block_count="$(((os64fs_hello_elf_length + OS64FS_DATA_BLOCK_SIZE - 1) / OS64FS_DATA_BLOCK_SIZE))"
os64fs_root_dir_bytes=$((3 * OS64FS_DIRECTORY_ENTRY_SIZE))
os64fs_docs_dir_bytes=$((4 * OS64FS_DIRECTORY_ENTRY_SIZE))
os64fs_inode_bitmap_offset=$((OS64FS_INODE_BITMAP_START_SECTOR * 512))
os64fs_data_bitmap_offset=$((OS64FS_DATA_BITMAP_START_SECTOR * 512))
os64fs_inode_table_offset=$((OS64FS_INODE_TABLE_START_SECTOR * 512))
os64fs_data_offset=$((OS64FS_DATA_START_SECTOR * 512))
os64fs_total_data_blocks=$((OS64FS_DATA_SECTOR_COUNT * 512 / OS64FS_DATA_BLOCK_SIZE))
os64fs_root_dir_block=0
os64fs_readme_block=1
os64fs_notes_block=2
os64fs_docs_dir_block=3
os64fs_guide_block=4
os64fs_hello_block=5
os64fs_hello_elf_first_block=6
os64fs_bigfile_indirect_block=$((os64fs_hello_elf_first_block + os64fs_hello_elf_block_count))
os64fs_bigfile_first_data_block=$((os64fs_bigfile_indirect_block + 1))
os64fs_used_data_blocks=$((os64fs_bigfile_first_data_block + OS64FS_BIGFILE_BLOCK_COUNT))
os64fs_root_dir_offset=$((os64fs_data_offset + os64fs_root_dir_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_readme_offset=$((os64fs_data_offset + os64fs_readme_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_notes_offset=$((os64fs_data_offset + os64fs_notes_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_docs_dir_offset=$((os64fs_data_offset + os64fs_docs_dir_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_guide_offset=$((os64fs_data_offset + os64fs_guide_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_hello_offset=$((os64fs_data_offset + os64fs_hello_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_hello_elf_offset=$((os64fs_data_offset + os64fs_hello_elf_first_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_bigfile_indirect_offset=$((os64fs_data_offset + os64fs_bigfile_indirect_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_bigfile_offset=$((os64fs_data_offset + os64fs_bigfile_first_data_block * OS64FS_DATA_BLOCK_SIZE))
os64fs_indirect_pointer_count=$((OS64FS_DATA_BLOCK_SIZE / 4))
os64fs_used_inode_count=8
os64fs_allocatable_inode_count=$((OS64FS_INODE_COUNT - 1))
os64fs_free_inode_count=$((os64fs_allocatable_inode_count - os64fs_used_inode_count))
os64fs_free_data_blocks=$((os64fs_total_data_blocks - os64fs_used_data_blocks))

if [ "$kernel_sectors" -le 0 ] || [ "$kernel_sectors" -gt "$max_kernel_sectors" ]; then
  # stage2 现在已经是“每轮只读 1 个扇区”的 CHS 循环，
  # 而且装载推进也已经处理了跨 64 KiB 时的 segment 进位，
  # 所以这里不再限制成 127 扇区，而是只要求“内核 + boot volume 还能装进整张软盘镜像”。
  echo "kernel.bin must occupy between 1 and $max_kernel_sectors sectors so the boot volume still fits in the floppy image, got $kernel_sectors sectors" >&2
  exit 1
fi

if [ "$os64fs_readme_length" -gt "$OS64FS_DATA_BLOCK_SIZE" ] || \
   [ "$os64fs_notes_length" -gt "$OS64FS_DATA_BLOCK_SIZE" ] || \
   [ "$os64fs_guide_length" -le 0 ] || \
   [ "$os64fs_guide_length" -gt "$OS64FS_DATA_BLOCK_SIZE" ] || \
   [ "$os64fs_hello_length" -le 0 ] || \
   [ "$os64fs_hello_length" -gt "$OS64FS_DATA_BLOCK_SIZE" ] || \
   [ "$os64fs_hello_elf_length" -le 0 ] || \
   [ "$os64fs_hello_elf_length" -gt $((OS64FS_DATA_BLOCK_SIZE * 4)) ] || \
   [ "$os64fs_hello_elf_block_count" -le 0 ] || \
   [ "$os64fs_hello_elf_block_count" -gt 8 ] || \
   [ "$os64fs_bigfile_length" -ne $((OS64FS_BIGFILE_BLOCK_COUNT * OS64FS_DATA_BLOCK_SIZE)) ] || \
   [ "$os64fs_used_data_blocks" -gt "$os64fs_total_data_blocks" ]; then
  echo "os64fs file sizes do not match the planned data block layout" >&2
  exit 1
fi

echo "[29/34] generating boot_volume.bin"
truncate -s 0 "$BOOT_VOLUME_BIN"
truncate -s "$BOOT_VOLUME_BYTES" "$BOOT_VOLUME_BIN"
printf '%s' "$OS64FS_SIGNATURE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=0 conv=notrunc status=none
write_le32 3 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=8 conv=notrunc status=none
write_le32 "$BOOT_VOLUME_SECTORS" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=12 conv=notrunc status=none
write_le32 "$OS64FS_INODE_BITMAP_START_SECTOR" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=16 conv=notrunc status=none
write_le32 "$OS64FS_INODE_BITMAP_SECTOR_COUNT" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=20 conv=notrunc status=none
write_le32 "$OS64FS_DATA_BITMAP_START_SECTOR" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=24 conv=notrunc status=none
write_le32 "$OS64FS_DATA_BITMAP_SECTOR_COUNT" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=28 conv=notrunc status=none
write_le32 "$OS64FS_INODE_TABLE_START_SECTOR" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=32 conv=notrunc status=none
write_le32 "$OS64FS_INODE_TABLE_SECTOR_COUNT" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=36 conv=notrunc status=none
write_le32 "$OS64FS_INODE_COUNT" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=40 conv=notrunc status=none
write_le32 "$OS64FS_INODE_SIZE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=44 conv=notrunc status=none
write_le32 "$OS64FS_DATA_START_SECTOR" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=48 conv=notrunc status=none
write_le32 "$OS64FS_DATA_SECTOR_COUNT" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=52 conv=notrunc status=none
write_le32 "$OS64FS_DATA_BLOCK_SIZE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=56 conv=notrunc status=none
write_le32 "$OS64FS_ROOT_INODE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=60 conv=notrunc status=none
write_le32 "$OS64FS_DIRECTORY_ENTRY_SIZE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=64 conv=notrunc status=none
write_le32 "$os64fs_free_inode_count" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=68 conv=notrunc status=none
write_le32 "$os64fs_free_data_blocks" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=72 conv=notrunc status=none
printf '%s' "$OS64FS_VOLUME_NAME" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=76 conv=notrunc status=none
write_le32 0 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=88 conv=notrunc status=none
write_le32 0 | dd of="$BOOT_VOLUME_BIN" bs=1 seek=92 conv=notrunc status=none

write_inode $((os64fs_inode_table_offset + 0 * OS64FS_INODE_SIZE)) 0 0 0 0 0 0 "$OS64FS_INVALID_BLOCK"
write_inode $((os64fs_inode_table_offset + 1 * OS64FS_INODE_SIZE)) 1 2 1 "$os64fs_root_dir_bytes" 493 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 1 * OS64FS_INODE_SIZE)) 0 "$os64fs_root_dir_block"
write_inode $((os64fs_inode_table_offset + 2 * OS64FS_INODE_SIZE)) 2 1 1 "$os64fs_readme_length" 420 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 2 * OS64FS_INODE_SIZE)) 0 "$os64fs_readme_block"
write_inode $((os64fs_inode_table_offset + 3 * OS64FS_INODE_SIZE)) 3 1 1 "$os64fs_notes_length" 420 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 3 * OS64FS_INODE_SIZE)) 0 "$os64fs_notes_block"
write_inode $((os64fs_inode_table_offset + 4 * OS64FS_INODE_SIZE)) 4 2 1 "$os64fs_docs_dir_bytes" 493 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 4 * OS64FS_INODE_SIZE)) 0 "$os64fs_docs_dir_block"
write_inode $((os64fs_inode_table_offset + 5 * OS64FS_INODE_SIZE)) 5 1 1 "$os64fs_guide_length" 420 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 5 * OS64FS_INODE_SIZE)) 0 "$os64fs_guide_block"
write_inode $((os64fs_inode_table_offset + 6 * OS64FS_INODE_SIZE)) 6 1 1 "$os64fs_hello_length" 493 1 "$OS64FS_INVALID_BLOCK"
write_inode_direct_block $((os64fs_inode_table_offset + 6 * OS64FS_INODE_SIZE)) 0 "$os64fs_hello_block"
write_inode $((os64fs_inode_table_offset + 7 * OS64FS_INODE_SIZE)) 7 1 1 "$os64fs_hello_elf_length" 493 "$os64fs_hello_elf_block_count" "$OS64FS_INVALID_BLOCK"
for hello_elf_block_slot in 0 1 2 3 4 5 6 7; do
  if [ "$hello_elf_block_slot" -ge "$os64fs_hello_elf_block_count" ]; then
    break
  fi
  write_inode_direct_block $((os64fs_inode_table_offset + 7 * OS64FS_INODE_SIZE)) "$hello_elf_block_slot" $((os64fs_hello_elf_first_block + hello_elf_block_slot))
done
write_inode $((os64fs_inode_table_offset + 8 * OS64FS_INODE_SIZE)) 8 1 1 "$os64fs_bigfile_length" 420 "$OS64FS_BIGFILE_BLOCK_COUNT" "$os64fs_bigfile_indirect_block"
for big_direct_slot in 0 1 2 3 4 5 6 7; do
  write_inode_direct_block $((os64fs_inode_table_offset + 8 * OS64FS_INODE_SIZE)) "$big_direct_slot" $((os64fs_bigfile_first_data_block + big_direct_slot))
done

write_dir_entry "$os64fs_root_dir_offset" 2 1 "readme.txt"
write_dir_entry $((os64fs_root_dir_offset + OS64FS_DIRECTORY_ENTRY_SIZE)) 3 1 "notes.txt"
write_dir_entry $((os64fs_root_dir_offset + OS64FS_DIRECTORY_ENTRY_SIZE * 2)) 4 2 "docs"
write_dir_entry "$os64fs_docs_dir_offset" 5 1 "guide.txt"
write_dir_entry $((os64fs_docs_dir_offset + OS64FS_DIRECTORY_ENTRY_SIZE)) 6 1 "hello.bin"
write_dir_entry $((os64fs_docs_dir_offset + OS64FS_DIRECTORY_ENTRY_SIZE * 2)) 7 1 "hello.elf"
write_dir_entry $((os64fs_docs_dir_offset + OS64FS_DIRECTORY_ENTRY_SIZE * 3)) 8 1 "$OS64FS_BIGFILE_NAME"

printf '%s' "$OS64FS_README" | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_readme_offset" conv=notrunc status=none
printf '%s' "$OS64FS_NOTES" | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_notes_offset" conv=notrunc status=none
printf '%s' "$OS64FS_GUIDE" | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_guide_offset" conv=notrunc status=none
dd if="$USER_HELLO_BIN" of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_hello_offset" conv=notrunc status=none
dd if="$USER_HELLO_ELF_BIN" of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_hello_elf_offset" conv=notrunc status=none
dd if="$os64fs_bigfile_tmp" of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_bigfile_offset" conv=notrunc status=none

indirect_slot=0
while [ "$indirect_slot" -lt "$os64fs_indirect_pointer_count" ]; do
  write_le32 "$OS64FS_INVALID_BLOCK" | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((os64fs_bigfile_indirect_offset + indirect_slot * 4)) conv=notrunc status=none
  indirect_slot=$((indirect_slot + 1))
done
write_le32 $((os64fs_bigfile_first_data_block + 8)) | dd of="$BOOT_VOLUME_BIN" bs=1 seek="$os64fs_bigfile_indirect_offset" conv=notrunc status=none
write_le32 $((os64fs_bigfile_first_data_block + 9)) | dd of="$BOOT_VOLUME_BIN" bs=1 seek=$((os64fs_bigfile_indirect_offset + 4)) conv=notrunc status=none

# v3 开始把“谁被占用、谁空闲”显式写进位图。
# 这一步虽然还没有真正的写支持，但挂载时已经能按正式文件系统的思路做一致性校验。
for inode_bit in 0 1 2 3 4 5 6 7 8; do
  write_bitmap_bit "$os64fs_inode_bitmap_offset" "$inode_bit"
done

data_block_bit=0
while [ "$data_block_bit" -lt "$os64fs_used_data_blocks" ]; do
  write_bitmap_bit "$os64fs_data_bitmap_offset" "$data_block_bit"
  data_block_bit=$((data_block_bit + 1))
done

# Stage2 needs to know how many sectors to read and what fixed address the kernel expects.
echo "[30/34] generating kernel metadata for stage2"
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
echo "[31/34] assembling stage2"
nasm -f bin -i "$BUILD_DIR/" "$STAGE2_SRC" -o "$STAGE2_BIN"

size="$(wc -c < "$STAGE2_BIN" | tr -d ' ')"
if [ "$size" -ne "$STAGE2_EXPECTED_SIZE" ]; then
  echo "stage2 size must be exactly $STAGE2_EXPECTED_SIZE bytes, got $size" >&2
  exit 1
fi

# Create a raw floppy-sized image and place stage1/stage2/kernel in the first sectors.
echo "[32/34] creating raw disk image"
truncate -s "$IMAGE_SIZE" "$DISK_IMG"
dd if="$STAGE1_BIN" of="$DISK_IMG" bs=512 count=1 conv=notrunc status=none
dd if="$STAGE2_BIN" of="$DISK_IMG" bs=512 seek=1 count=8 conv=notrunc status=none
dd if="$KERNEL_BIN" of="$DISK_IMG" bs=512 seek=9 conv=notrunc status=none
dd if="$BOOT_VOLUME_BIN" of="$DISK_IMG" bs=512 seek=$((boot_volume_start_sector - 1)) conv=notrunc status=none

# BIOS boot sectors must end with the 0x55aa signature.
echo "[33/34] verifying boot signature"
signature="$(hexdump -n 2 -s 510 -e '2/1 "%02x"' "$STAGE1_BIN")"
if [ "$signature" != "55aa" ]; then
  echo "boot signature mismatch: expected 55aa, got $signature" >&2
  exit 1
fi

# Emit the key output paths so manual runs can inspect the artifacts directly.
echo "[34/34] build complete"
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
