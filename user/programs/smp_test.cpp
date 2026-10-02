#include "os64.hpp"
#include "smp.hpp"
#include "memory.hpp"
#include "bench_workload.hpp"

namespace {
constexpr size_t kWorkers = 12, kRounds = 6;
constexpr uint64_t kIterations = 20000000;
struct Progress { uint64_t worker, round, cpu, tick, result; };
bool equal(const char* a,const char* b) { while(*a && *a==*b) { ++a; ++b; } return *a==*b; }
void decimal(uint64_t value,char* output) {
  char digits[21]; size_t n=0;
  do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(n) output[i++]=digits[--n]; output[i]=0;
}
void field(const char* label,uint64_t value) { print(label); number(value); print("\n"); }
int worker(uint64_t id,int read_fd,int write_fd,uint64_t deadline) {
  if(id>=kWorkers) return 10;
  close(read_fd);
  SmpSnapshot initial{}, observed{};
  if(smp_snapshot(&initial)!=0 || initial.current_cpu>=initial.online_cpus) return 11;
  while(ticks()<deadline) sleep(1);
  uint64_t result=id+123;
  for(uint64_t round=1;round<=kRounds;++round) {
    // No voluntary yield while calculating: local timer preemption must work.
    auto* memory_bytes=static_cast<volatile uint8_t*>(memory::allocate(65536));
    if(memory_bytes==nullptr) return 14;
    for(size_t offset=0;offset<65536;++offset) memory_bytes[offset]=static_cast<uint8_t>(id*17+round+offset);
    result=bench_compute(kIterations,result);
    for(size_t offset=0;offset<65536;++offset)
      if(memory_bytes[offset]!=static_cast<uint8_t>(id*17+round+offset)) return 15;
    memory::release(const_cast<uint8_t*>(memory_bytes));
    if(smp_snapshot(&observed)!=0 || observed.current_cpu!=initial.current_cpu) return 12;
    Progress progress{id,round,observed.current_cpu,ticks(),result};
    if(write(write_fd,&progress,sizeof(progress))!=sizeof(progress)) return 13;
    if(round&1) sleep(1); // Deadline wake on BSP must reschedule an AP-pinned thread.
  }
  close(write_fd);
  return 0;
}
}
extern "C" int main(int argc,char** argv) {
  if(argc==6 && equal(argv[1],"worker"))
    return worker(parse_number(argv[2]),parse_number(argv[3]),parse_number(argv[4]),parse_number(argv[5]));
  if(argc!=1) return 1;
  PerformanceSnapshot before{},after{};
  SmpSnapshot cpu_before{},cpu_after{};
  if(perf_snapshot(&before)!=0 || smp_snapshot(&cpu_before)!=0 ||
     cpu_before.abi_version!=1 || cpu_before.online_cpus<1 || cpu_before.online_cpus>4 ||
     cpu_before.online_mask!=(1ULL<<cpu_before.online_cpus)-1) return 2;
  int32_t ends[2]; if(pipe(ends)!=0) return 3;
  char read_text[21],write_text[21],deadline_text[21],id_text[kWorkers][21];
  decimal(ends[0],read_text); decimal(ends[1],write_text); decimal(ticks()+50,deadline_text);
  int64_t pids[kWorkers]; bool ok=true;
  for(size_t i=0;i<kWorkers;++i) {
    decimal(i,id_text[i]);
    const char* args[]={argv[0],"worker",id_text[i],read_text,write_text,deadline_text};
    pids[i]=spawn(argv[0],args,6); if(pids[i]<0) ok=false;
  }
  close(ends[1]);
  size_t rounds[kWorkers]{}; uint64_t assigned[kWorkers], previous_tick[kWorkers]{};
  for(auto& cpu:assigned) cpu=UINT64_MAX;
  uint64_t used_mask=0; Progress progress{}; size_t buffered=0; int64_t count;
  while((count=read(ends[0],reinterpret_cast<uint8_t*>(&progress)+buffered,sizeof(progress)-buffered))>0) {
    buffered+=static_cast<size_t>(count); if(buffered<sizeof(progress)) continue;
    buffered=0;
    if(progress.worker>=kWorkers || progress.cpu>=cpu_before.online_cpus) { ok=false; continue; }
    const size_t id=progress.worker;
    if(assigned[id]==UINT64_MAX) assigned[id]=progress.cpu;
    if(progress.cpu!=assigned[id] || progress.round!=rounds[id]+1 || progress.round>kRounds ||
       progress.tick<previous_tick[id] ||
       progress.result!=bench_expected(progress.round*kIterations,id+123)) ok=false;
    rounds[id]=progress.round; previous_tick[id]=progress.tick;
    used_mask|=1ULL<<progress.cpu;
  }
  close(ends[0]); if(count<0 || buffered!=0) ok=false;
  for(size_t i=0;i<kWorkers;++i) if(pids[i]>=0) {
    int32_t status=-1;
    if(waitpid(pids[i],&status)!=pids[i] || status!=0 || rounds[i]!=kRounds) ok=false;
  }
  // These children are assigned away from the live parent when APs exist.
  // Their faults must close inherited FDs and reclaim their private stacks/pages.
  for(size_t repeat=0;repeat<4;++repeat) {
    const char* fault_paths[]={"/bin/stackfault", "/bin/nxfault"};
    for(const char* path : fault_paths) {
      const char* args[]={path}; int64_t pid=spawn(path,args,1); int32_t status=-1;
      if(pid<0 || waitpid(pid,&status)!=pid || status!=142) ok=false;
    }
  }
  if(perf_snapshot(&after)!=0 || smp_snapshot(&cpu_after)!=0 ||
     after.free_pages!=before.free_pages || after.heap_used_bytes!=before.heap_used_bytes ||
     after.preempt_requests<=before.preempt_requests || used_mask!=cpu_before.online_mask) ok=false;
  for(size_t cpu=0;cpu<cpu_before.online_cpus;++cpu)
    if(cpu_after.user_dispatches[cpu]<=cpu_before.user_dispatches[cpu] ||
       cpu_after.user_ticks[cpu]<=cpu_before.user_ticks[cpu]) ok=false;
  field("smp_test online_cpus=",cpu_before.online_cpus);
  field("smp_test worker_cpu_mask=",used_mask);
  field("smp_test free_pages_before=",before.free_pages);
  field("smp_test free_pages_after=",after.free_pages);
  print(ok?"smp_test twelve_workers_pin_compute_wake_resources_ok\n":"smp_test: CPU, checksum, wake or resource check failed\n");
  return ok?0:4;
}
