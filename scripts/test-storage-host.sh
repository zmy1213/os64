#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
HOST_CXX="${HOST_CXX:-$(find_tool clang++ g++)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
mkdir -p "$BUILD_DIR"
"$HOST_CXX" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT_DIR/kernel" "$ROOT_DIR/tests/storage_host.cpp" \
  "$ROOT_DIR/kernel/fs/os64fs.cpp" "$ROOT_DIR/kernel/fs/file.cpp" \
  "$ROOT_DIR/kernel/fs/directory.cpp" "$ROOT_DIR/kernel/fs/vfs.cpp" \
  "$ROOT_DIR/kernel/storage/block_device.cpp" "$ROOT_DIR/kernel/storage/boot_volume.cpp" \
  "$ROOT_DIR/kernel/runtime/runtime.cpp" -o "$BUILD_DIR/storage-host-test"
"$BUILD_DIR/storage-host-test" "$BUILD_DIR/boot_volume.bin"
