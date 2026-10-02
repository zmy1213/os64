#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT_DIR/scripts/toolchain.sh"
resolve_build_tools
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
LINUX_DIR="$BUILD_DIR/linux-benchmark"
mkdir -p "$LINUX_DIR"
# Match scripts/build-user.sh; only the syscall/startup adapter differs.
FLAGS=( -I "$ROOT_DIR/user" -I "$ROOT_DIR/tools" -ffreestanding -fno-exceptions
  -fno-rtti -fno-stack-protector -fno-pic -fno-pie -mno-red-zone
  -mgeneral-regs-only -fno-builtin -Os -Wall -Wextra
  -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-omit-frame-pointer )
if [[ "$CLANGXX_BIN" == *clang++* ]]; then
  FLAGS=( --target=x86_64-linux-gnu "${FLAGS[@]}" )
fi
"$NASM_BIN" -f elf64 "$ROOT_DIR/tools/linux_bench_start.asm" -o "$LINUX_DIR/start.o"
"$CLANGXX_BIN" "${FLAGS[@]}" -c "$ROOT_DIR/tools/linux_bench_runtime.cpp" -o "$LINUX_DIR/runtime.o"
for program in bench bench_ipc; do
  "$CLANGXX_BIN" "${FLAGS[@]}" -DOS64_LINUX_BENCH -c "$ROOT_DIR/user/programs/$program.cpp" -o "$LINUX_DIR/$program.o"
  "$LD_BIN" -m elf_x86_64 -nostdlib -static -e _start -o "$LINUX_DIR/$program" \
    "$LINUX_DIR/start.o" "$LINUX_DIR/$program.o" "$LINUX_DIR/runtime.o"
done
"$PYTHON_BIN" - "$LINUX_DIR" "$CLANGXX_BIN" "${FLAGS[@]}" <<'PY'
import json, subprocess, sys
from pathlib import Path
metadata = {'compiler': subprocess.check_output([sys.argv[2], '--version'], text=True).splitlines()[0],
            'flags': sys.argv[3:]}
(Path(sys.argv[1]) / 'build-info.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
"$PYTHON_BIN" "$ROOT_DIR/scripts/make-linux-benchmark-initramfs.py" \
  --build-dir "$BUILD_DIR" --assets "${BENCHMARK_ASSETS:-$ROOT_DIR/build/benchmark-deps}"
