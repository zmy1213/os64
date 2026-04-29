#ifndef OS64_RUNTIME_HPP
#define OS64_RUNTIME_HPP

#include <stddef.h>
#include <stdint.h>

// 这一轮先只补最小运行时里最常用的两样：
// 清零一段内存，和把一段内存从 A 复制到 B。
void* memory_set(void* destination, uint8_t value, size_t size);
void* memory_copy(void* destination, const void* source, size_t size);

#endif
