#ifndef OS64_USER_MEMORY_HPP
#define OS64_USER_MEMORY_HPP
#include <stddef.h>

// 这是教程自己的小内存库，不是完整的 libc。这里的指针属于当前进程。
// allocate(0) 返回 nullptr；release(nullptr) 什么也不做。
// release 的参数只能是本库返回且尚未释放的指针。
namespace memory {
void* allocate(size_t bytes);
void release(void* pointer);
// 调整大小会保留 min(旧大小, 新大小) 个字节。
// 失败返回 nullptr，原来的 pointer 仍然有效；resize(pointer,0) 会释放。
void* resize(void* pointer, size_t bytes);
}

// 编译器也会把“复制结构体”等表达式翻译为这些函数，因此单独实现它们。
extern "C" void* memcpy(void* destination, const void* source, size_t bytes);
extern "C" void* memmove(void* destination, const void* source, size_t bytes);
extern "C" void* memset(void* destination, int value, size_t bytes);
extern "C" int memcmp(const void* left, const void* right, size_t bytes);
#endif
