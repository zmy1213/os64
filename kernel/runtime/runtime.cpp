#include "runtime/runtime.hpp"

void* memory_set(void* destination, uint8_t value, size_t size) {
  auto* bytes = static_cast<uint8_t*>(destination);

  // 最基础的逐字节写入版本。
  // 不追求性能，只追求 freestanding 环境下一定可用、语义清楚。
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = value;
  }

  return destination;
}

void* memory_copy(void* destination, const void* source, size_t size) {
  auto* dst = static_cast<uint8_t*>(destination);
  const auto* src = static_cast<const uint8_t*>(source);

  // 同样先保留最简单可靠的逐字节复制实现。
  for (size_t i = 0; i < size; ++i) {
    dst[i] = src[i];
  }

  return destination;
}
