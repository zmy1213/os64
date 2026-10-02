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

// Freestanding C++ may lower aggregate initialization to these C ABI symbols.
#if !__STDC_HOSTED__
extern "C" void* memset(void* destination, int value, size_t size) {
  return memory_set(destination, static_cast<uint8_t>(value), size);
}

extern "C" void* memcpy(void* destination, const void* source, size_t size) {
  return memory_copy(destination, source, size);
}

extern "C" void* memmove(void* destination, const void* source, size_t size) {
  auto* dst = static_cast<uint8_t*>(destination);
  const auto* src = static_cast<const uint8_t*>(source);
  if (reinterpret_cast<uintptr_t>(dst) > reinterpret_cast<uintptr_t>(src)) {
    while (size != 0) { --size; dst[size] = src[size]; }
  } else {
    for (size_t i = 0; i < size; ++i) { dst[i] = src[i]; }
  }
  return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t size) {
  const auto* lhs = static_cast<const uint8_t*>(left);
  const auto* rhs = static_cast<const uint8_t*>(right);
  for (size_t i = 0; i < size; ++i) {
    if (lhs[i] != rhs[i]) { return lhs[i] < rhs[i] ? -1 : 1; }
  }
  return 0;
}
#endif

void* memory_copy(void* destination, const void* source, size_t size) {
  auto* dst = static_cast<uint8_t*>(destination);
  const auto* src = static_cast<const uint8_t*>(source);

  // 同样先保留最简单可靠的逐字节复制实现。
  for (size_t i = 0; i < size; ++i) {
    dst[i] = src[i];
  }

  return destination;
}
