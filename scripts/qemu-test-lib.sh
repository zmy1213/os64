#!/usr/bin/env bash
set -euo pipefail

# 这些辅助函数给 `scripts/test-*.sh` 共用。
# 重点不是“增加抽象层”，而是把等待 QEMU 的方式从固定 `sleep N`
# 改成“在一个有限超时窗口里轮询串口日志是否已经满足目标里程碑”。

wait_for_serial_markers() {
  local serial_log="$1"
  local qemu_pid="$2"
  local max_attempts="$3"
  local check_function="$4"
  local attempt

  for ((attempt = 0; attempt < max_attempts; ++attempt)); do
    # 只要日志已经存在，并且调用方认定“该有的关键输出都齐了”，
    # 当前测试就可以提前收尾。
    if [ -f "$serial_log" ] && "$check_function" "$serial_log"; then
      return 0
    fi

    # 如果 QEMU 已经退出，就没必要继续轮询；
    # 这可能表示它主动走到了 debug-exit，也可能表示提前失败。
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
      break
    fi

    sleep 0.1
  done

  # 再做最后一次检查，覆盖“QEMU 刚好在上一次循环末尾退出，但日志已经写完”的情况。
  if [ -f "$serial_log" ] && "$check_function" "$serial_log"; then
    return 0
  fi

  return 1
}

stop_qemu_if_running() {
  local qemu_pid="$1"

  kill "$qemu_pid" 2>/dev/null || true
  wait "$qemu_pid" 2>/dev/null || true
}
