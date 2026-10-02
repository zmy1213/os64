#include "os64.hpp"

namespace {
bool equal(const char* a,const char* b) {
  while(*a && *a==*b) {++a;++b;} return *a==*b;
}
bool worker(uint64_t seed) {
  alignas(16) uint8_t initial[512];
  asm volatile("fxsave64 %0" : "=m"(initial) : : "memory");
  if(*reinterpret_cast<uint16_t*>(initial)!=0x37f || initial[4]!=0 ||
     *reinterpret_cast<uint32_t*>(initial+24)!=0x1f80) return false;
  for(size_t i=160;i<416;++i) if(initial[i]!=0) return false;
  for(size_t slot=0;slot<8;++slot)
    for(size_t byte=0;byte<10;++byte) if(initial[32+slot*16+byte]!=0) return false;
  const uint16_t control=static_cast<uint16_t>(0x37f|((seed&1)?0x400:0));
  const uint32_t mxcsr=0x1f80|((seed&1)?0x2000:0);
  alignas(16) uint64_t lanes[2]={seed,seed*3};
  alignas(16) uint64_t high[2]={seed*5,seed*7};
  alignas(16) uint64_t increments[2]={1,2};
  int64_t floating=static_cast<int64_t>(seed);
  asm volatile("fldcw %0; ldmxcsr %1; fildq %2; movdqu %3,%%xmm0; movdqu %4,%%xmm15"
               : : "m"(control),"m"(mxcsr),"m"(floating),"m"(lanes),"m"(high) : "memory");
  uint64_t count=0;
  for(size_t round=0;round<48;++round) {
    // Long uninterrupted runs exercise timer preemption as well as explicit yield.
    for(size_t i=0;i<65536;++i)
      asm volatile("paddq %0,%%xmm0; paddq %0,%%xmm15; fld1; faddp"
                   : : "m"(increments) : "memory");
    count+=65536;
    yield();
    if(round%8==0) sleep(1);
    alignas(16) uint64_t observed[2],observed_high[2];
    uint16_t observed_control; uint32_t observed_mxcsr;
    asm volatile("movdqu %%xmm0,%0; movdqu %%xmm15,%1; fistpq %2; fildq %2; fnstcw %3; stmxcsr %4"
                 : "=m"(observed),"=m"(observed_high),"=m"(floating),
                   "=m"(observed_control),"=m"(observed_mxcsr) : : "memory");
    if(observed[0]!=seed+count || observed[1]!=seed*3+count*2 ||
       observed_high[0]!=seed*5+count || observed_high[1]!=seed*7+count*2 ||
       floating!=static_cast<int64_t>(seed+count) || observed_control!=control ||
       observed_mxcsr!=mxcsr) return false;
  }
  return true;
}
}
extern "C" int main(int argc,char** argv) {
  if(argc==3 && equal(argv[1],"worker")) return worker(parse_number(argv[2]))?0:10;
  if(argc!=1) return 1;
  PerformanceSnapshot before{},after{};
  if(perf_snapshot(&before)!=0 || (before.cpu_features&15)!=15) {
    error("fp_test: x87/FXSAVE/SSE2 unavailable\n"); return 2;
  }
  const char* seeds[]={"101","202","303","404"};
  int64_t pids[4]={-1,-1,-1,-1}; bool ok=true;
  for(size_t i=0;i<4;++i) {
    const char* args[]={argv[0],"worker",seeds[i]};
    pids[i]=spawn(argv[0],args,3);
    if(pids[i]<0) {ok=false;break;}
  }
  for(auto pid:pids) if(pid>=0) {
    int32_t status=-1; if(waitpid(pid,&status)!=pid || status!=0) ok=false;
  }
  if(perf_snapshot(&after)!=0) return 3;
  if(after.preempt_requests<=before.preempt_requests) {
    error("fp_test: timer preemption was not exercised\n"); return 4;
  }
  print(ok?"fp_test SSE_x87_isolation_ok\n":"fp_test: floating context corrupted\n");
  return ok?0:5;
}
