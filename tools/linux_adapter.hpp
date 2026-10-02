#ifndef OS64_LINUX_BENCH_ADAPTER_HPP
#define OS64_LINUX_BENCH_ADAPTER_HPP
#include <stddef.h>
#include <stdint.h>
#include "perf.hpp"

// A tiny Linux ABI adapter, not a second workload. bench.cpp and its integer
// loop are compiled unchanged. The guest binary uses no host or Linux libc.
inline int64_t linux_call(uint64_t n,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0) {
  register uint64_t fourth asm("r10")=d;
  int64_t result;
  asm volatile("syscall" : "=a"(result) : "a"(n),"D"(a),"S"(b),"d"(c),"r"(fourth)
               : "rcx","r11","memory");
  return result;
}
inline void print(const char* text) {
  size_t size=0; while(text[size]) ++size;
  while(size) { const int64_t n=linux_call(1,1,reinterpret_cast<uint64_t>(text),size);
    if(n<=0) return; text+=n; size-=static_cast<size_t>(n); }
}
inline void error(const char* text) { print(text); }
inline void number(uint64_t value) {
  char reverse[21],output[21]; size_t size=0;
  do { reverse[size++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(size) output[i++]=reverse[--size]; output[i]='\0'; print(output);
}
inline int64_t yield() { return linux_call(24); }
inline int64_t read(int fd,void* buffer,size_t size) {
  return linux_call(0,fd,reinterpret_cast<uint64_t>(buffer),size);
}
inline int64_t write(int fd,const void* buffer,size_t size) {
  return linux_call(1,fd,reinterpret_cast<uint64_t>(buffer),size);
}
inline int64_t close(int fd) { return linux_call(3,fd); }
inline int64_t pipe(int32_t ends[2]) {
  const int64_t result=linux_call(22,reinterpret_cast<uint64_t>(ends));
  if(result!=0) return result;
  // Linux defaults vary; normalize the live pipe to OS64's 4096-byte capacity.
  const int64_t capacity=linux_call(72,ends[1],1031,4096); // F_SETPIPE_SZ
  if(capacity!=4096) { close(ends[0]); close(ends[1]); return -1; }
  return 0;
}
inline int64_t spawn(const char* path,const char* const* args,uint64_t) {
  const int64_t pid=linux_call(57);
  if(pid==0) {
    const char* environment[]={nullptr};
    linux_call(59,reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(args),
               reinterpret_cast<uint64_t>(environment));
    linux_call(60,254);
    __builtin_unreachable();
  }
  return pid;
}
inline int64_t waitpid(int64_t pid,int32_t* status) {
  int32_t raw=0;
  const int64_t waited=linux_call(61,pid,reinterpret_cast<uint64_t>(&raw),0,0);
  if(waited>=0 && status) *status=(raw&127)?128+(raw&127):(raw>>8)&255;
  return waited;
}
inline int64_t perf_snapshot(PerformanceSnapshot* output) {
  for(size_t i=0;i<sizeof(*output);++i) reinterpret_cast<uint8_t*>(output)[i]=0;
  struct Timespec { int64_t seconds,nanoseconds; } time{};
  if(linux_call(228,1,reinterpret_cast<uint64_t>(&time))!=0) return -1;
  output->abi_version=1;
  output->timer_hz=100;
  output->ticks=static_cast<uint64_t>(time.seconds)*100+time.nanoseconds/10000000;
  // Linux rusage is 18 signed 64-bit words: timeval[2], then 14 counters.
  // SELF+reaped CHILDREN gives an aggregate, unlike OS64's all-thread counter.
  int64_t usage[18]{};
  if(linux_call(98,0,reinterpret_cast<uint64_t>(usage))==0)
    output->context_switches=usage[16]+usage[17];
  if(linux_call(98,static_cast<uint64_t>(-1),reinterpret_cast<uint64_t>(usage))==0)
    output->context_switches+=usage[16]+usage[17];
  return 0;
}
#endif
