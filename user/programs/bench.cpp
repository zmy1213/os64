#if defined(OS64_LINUX_BENCH)
#include "linux_adapter.hpp"
#else
#include "os64.hpp"
#endif
#include "bench_workload.hpp"

namespace {
bool equal(const char* a,const char* b) {
  while(*a && *a==*b) { ++a; ++b; }
  return *a==*b;
}
bool integer(const char* text,uint64_t* result) {
  if(!*text) return false;
  uint64_t value=0;
  while(*text) {
    if(*text<'0' || *text>'9' || value>(UINT64_MAX-(*text-'0'))/10) return false;
    value=value*10+(*text++-'0');
  }
  *result=value; return true;
}
void decimal(uint64_t value,char* output) {
  char reverse[21]; size_t n=0;
  do { reverse[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(n) output[i++]=reverse[--n]; output[i]='\0';
}
uint64_t compute(uint64_t count,uint64_t seed,uint64_t yield_every) {
  if(!yield_every) return bench_compute(count,seed);
  while(count) {
    const uint64_t chunk=count<yield_every?count:yield_every;
    seed=bench_compute(chunk,seed); count-=chunk;
    if(count) yield();
  }
  return seed;
}
void field(const char* label,uint64_t value) { print(label); number(value); print("\n"); }
}
extern "C" int main(int argc,char** argv) {
  if(argc==5 && equal(argv[1],"worker")) {
    uint64_t iterations,seed,yield_every;
    if(!integer(argv[2],&iterations) || !integer(argv[3],&seed) ||
       !integer(argv[4],&yield_every)) return 253;
    const uint64_t result=compute(iterations,seed,yield_every);
    if(result!=bench_expected(iterations,seed)) return 252;
    return static_cast<int>(bench_checksum(result));
  }
  uint64_t workers=4,iterations=50000000,yield_every=0;
  if(argc>4 || (argc>1 && !integer(argv[1],&workers)) ||
     (argc>2 && !integer(argv[2],&iterations)) ||
     (argc>3 && !integer(argv[3],&yield_every)) ||
     workers<1 || workers>12 || iterations<1 || iterations>500000000) {
    error("usage: run /bin/bench [workers 1..12] [iterations 1..500000000] [yield_every]\n");
    return 1;
  }
  char count_text[21],yield_text[21],seed_text[12][21];
  decimal(iterations,count_text); decimal(yield_every,yield_text);
  uint32_t expected[12];
  for(size_t i=0;i<workers;++i) {
    const uint64_t seed=123456789+i;
    decimal(seed,seed_text[i]); expected[i]=bench_checksum(bench_expected(iterations,seed));
  }
  PerformanceSnapshot before{},after{};
  if(perf_snapshot(&before)!=0) return 2;
  int64_t pids[12];
  for(size_t i=0;i<12;++i) pids[i]=-1;
  bool ok=true;
  for(size_t i=0;i<workers;++i) {
    const char* arguments[]={argv[0],"worker",count_text,seed_text[i],yield_text,nullptr};
    pids[i]=spawn(argv[0],arguments,5);
    if(pids[i]<0) { ok=false; break; }
  }
  uint64_t checksum=0;
  for(size_t i=0;i<workers;++i) if(pids[i]>=0) {
    int32_t status=-1;
    if(waitpid(pids[i],&status)!=pids[i] || status!=static_cast<int32_t>(expected[i])) ok=false;
    checksum+=static_cast<uint32_t>(status);
  }
  if(perf_snapshot(&after)!=0) return 3;
  // No worker output: UART writes must not dominate the calculation being timed.
  field("bench workers=",workers);
  field("bench iterations_per_worker=",iterations);
  field("bench yield_every=",yield_every);
  field("bench elapsed_ticks=",after.ticks-before.ticks);
  field("bench timer_hz=",after.timer_hz);
  field("bench context_switches=",after.context_switches-before.context_switches);
  field("bench preempt_requests=",after.preempt_requests-before.preempt_requests);
  field("bench checksum8_sum=",checksum);
  field("bench free_pages_before=",before.free_pages);
  field("bench free_pages_after=",after.free_pages);
  print(ok?"bench correctness=ok\n":"bench correctness=FAILED\n");
  return ok?0:4;
}
