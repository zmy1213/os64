#include "log/log.hpp"
#include "bench_workload.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
static uint64_t clock_ticks=0;
uint64_t timer_tick_count() { return clock_ticks; }
void* memory_set(void* destination,uint8_t value,size_t size) { return std::memset(destination,value,size); }
void* memory_copy(void* destination,const void* source,size_t size) { return std::memcpy(destination,source,size); }
int main() {
  kernel_log_initialize();
  KernelLogRecord output[kKernelLogCapacity+1]{};
  assert(kernel_log_read(0,output,257)==0);
  assert(kernel_log_read(0,nullptr,1)==0);
  clock_ticks=17;
  kernel_log_write(kLogInfo,"cpu","ready");
  assert(kernel_log_read(0,output,1)==1);
  assert(output[0].sequence==1 && output[0].ticks==17 && output[0].level==kLogInfo);
  assert(std::strcmp(output[0].component,"cpu")==0 && std::strcmp(output[0].message,"ready")==0);
  assert(output[0].reserved==0 && kernel_log_read(1,output,1)==0);
  char long_text[200]; std::memset(long_text,'x',199); long_text[199]=0;
  kernel_log_write(kLogError,long_text,long_text);
  assert(kernel_log_read(1,output,1)==1);
  assert(std::strlen(output[0].component)==15 && std::strlen(output[0].message)==87);
  for(uint64_t i=3;i<=300;++i) { clock_ticks=i*2; kernel_log_write(kLogDebug,nullptr,nullptr); }
  auto stats=kernel_log_stats();
  assert(stats.count==256 && stats.next_sequence==301 && stats.overwritten==44);
  assert(kernel_log_read(0,output,257)==256);
  for(size_t i=0;i<256;++i) {
    assert(output[i].sequence==i+45 && output[i].ticks==(i+45)*2);
    assert(output[i].component[0]==0 && output[i].message[0]==0 && output[i].reserved==0);
  }
  assert(kernel_log_read(299,output,257)==1 && output[0].sequence==300);
  assert(kernel_log_read(UINT64_MAX,output,1)==0);
  assert(std::strcmp(kernel_log_level_name(99),"unknown")==0);
  kernel_log_initialize(); assert(kernel_log_stats().overwritten==0);
  char event[65]; std::memset(event,'x',sizeof(event));
  kernel_log_process_event(kLogDebug,event,UINT32_MAX);
  assert(kernel_log_read(0,output,1)==1);
  assert(std::strcmp(output[0].component,"process")==0);
  assert(std::strlen(output[0].message)==80);
  assert(std::strcmp(output[0].message+65," pid=4294967295")==0);
  // Validate the independent affine verifier against direct computation before
  // relying on it to check large timed workloads in either guest.
  const uint64_t seeds[]={0,1,123456789,UINT64_MAX};
  for(uint64_t seed: seeds)
    for(uint64_t n=0;n<10000;n+=97) assert(bench_compute(n,seed)==bench_expected(n,seed));
  std::puts("log ring: order, overwrite, truncation, cursors; workload verifier: passed");
}
