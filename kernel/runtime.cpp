#include "runtime.hpp"

void* memory_set(void* destination, uint8_t value, size_t size) {
  auto* bytes = static_cast<uint8_t*>(destination);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = value;
  }

  return destination;
}

void* memory_copy(void* destination, const void* source, size_t size) {
  auto* dst = static_cast<uint8_t*>(destination);
  const auto* src = static_cast<const uint8_t*>(source);

  for (size_t i = 0; i < size; ++i) {
    dst[i] = src[i];
  }

  return destination;
}
