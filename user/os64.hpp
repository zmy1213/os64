#ifndef OS64_USER_HPP
#define OS64_USER_HPP
#include <stddef.h>
#include <stdint.h>
#include "perf.hpp"

// int 0x80: RAX number, RDI/RSI/RDX/RCX/R8 arguments, RAX signed result.
inline int64_t syscall(uint64_t number, uint64_t a=0, uint64_t b=0,
                       uint64_t c=0, uint64_t d=0, uint64_t e=0) {
  register uint64_t rax asm("rax") = number;
  register uint64_t r8 asm("r8") = e;
  asm volatile("int $0x80" : "+a"(rax) : "D"(a), "S"(b), "d"(c), "c"(d), "r"(r8)
               : "memory", "cc");
  return static_cast<int64_t>(rax);
}
inline size_t length(const char* s) { size_t n=0; while (s[n]) ++n; return n; }
inline int64_t write(int fd, const void* data, size_t size) {
  return syscall(9, fd, reinterpret_cast<uint64_t>(data), size);
}
inline void print(const char* s) { write(1, s, length(s)); }
inline void error(const char* s) { write(2, s, length(s)); }
inline void number(uint64_t value) {
  char digits[21]; size_t count=0;
  do { digits[count++] = static_cast<char>('0'+value%10); value/=10; } while(value);
  while(count) { char digit=digits[--count]; write(1,&digit,1); }
}
inline uint64_t parse_number(const char* value) {
  uint64_t n=0; while(*value>='0' && *value<='9') { n=n*10+(*value++-'0'); } return n;
}
inline int64_t open(const char* path, uint64_t flags=0) {
  return syscall(19, reinterpret_cast<uint64_t>(path),flags);
}
inline int64_t read(int fd, void* data, size_t size) {
  return syscall(3,fd,reinterpret_cast<uint64_t>(data),size);
}
inline int64_t close(int fd) { return syscall(4,fd); }
inline int64_t pipe(int32_t descriptors[2]) {
  return syscall(22,reinterpret_cast<uint64_t>(descriptors));
}
inline int64_t dup2(int old_fd, int new_fd) { return syscall(23,old_fd,new_fd); }
inline int64_t dup(int old_fd) { return syscall(24,old_fd); }
constexpr int64_t BROKEN_PIPE=-7;
inline int64_t yield() { return syscall(11); }
inline int64_t sleep(uint64_t ms) { return syscall(14,ms); }
inline int64_t sync() { return syscall(18); }
inline uint64_t ticks() { return static_cast<uint64_t>(syscall(30)); }
inline int64_t read_log(KernelLogRecord* records, size_t count, uint64_t after_sequence=0) {
  return syscall(31,reinterpret_cast<uint64_t>(records),count,after_sequence);
}
inline int64_t perf_snapshot(PerformanceSnapshot* output) {
  return syscall(32,reinterpret_cast<uint64_t>(output));
}
// break 是“当前用户堆的末尾地址”，不是已经使用的字节数。
// brk(0) 查询末尾；失败仍返回旧末尾，所以调用方必须比较返回值。
inline uintptr_t brk(uintptr_t new_break=0) {
  return static_cast<uintptr_t>(syscall(20,new_break));
}
// 完整替换文件，返回字节数；失败返回负错误码。
// 本次操作用一笔文件系统事务完成，但还没有突然断电后的日志恢复。
inline int64_t replace_file(const char* path, const void* data, size_t size) {
  return syscall(21,reinterpret_cast<uint64_t>(path),
                 reinterpret_cast<uint64_t>(data),size);
}
inline int64_t spawn(const char* path, const char* const* argv, size_t argc) {
  return syscall(15,reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(argv),argc);
}
inline int64_t waitpid(int64_t pid, int32_t* status) {
  return syscall(16,pid,reinterpret_cast<uint64_t>(status));
}
struct DirectoryEntry {
  uint32_t inode_number; uint16_t type; uint8_t name_length;
  char name[57]; uint32_t size_bytes;
};
static_assert(sizeof(DirectoryEntry)==68,"directory ABI must match VfsDirectoryEntry");
inline int64_t listdir(const char* path, DirectoryEntry* entries, size_t count) {
  return syscall(8,reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(entries),count);
}
constexpr uint64_t READ=1, WRITE=2, CREATE=4, TRUNC=8, APPEND=16;
#endif
