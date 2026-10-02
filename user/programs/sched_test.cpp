#include "os64.hpp"
#include "bench_workload.hpp"
namespace {
constexpr size_t kWorkers=8,kRounds=6;
constexpr uint64_t kIterations=20000000;
struct Progress { uint64_t worker,round,ticks,result; };
bool equal(const char* a,const char* b) { while(*a && *a==*b) { ++a; ++b; } return *a==*b; }
void decimal(uint64_t value,char* output) {
  char digits[21]; size_t n=0;
  do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(n) output[i++]=digits[--n]; output[i]=0;
}
int worker(uint64_t id,int read_fd,int write_fd,uint64_t deadline) {
  close(read_fd);
  while(ticks()<deadline) sleep(1);
  uint64_t result=id+123;
  for(uint64_t round=1;round<=kRounds;++round) {
    result=bench_compute(kIterations,result);
    Progress progress{id,round,ticks(),result};
    // One 32-byte write is atomic in the 4096-byte teaching pipe. No yield is
    // used in the compute phase: progress depends on timer-driven preemption.
    if(write(write_fd,&progress,sizeof(progress))!=sizeof(progress)) return 10;
  }
  close(write_fd); return 0;
}
}
extern "C" int main(int argc,char** argv) {
  if(argc==6 && equal(argv[1],"worker"))
    return worker(parse_number(argv[2]),parse_number(argv[3]),parse_number(argv[4]),parse_number(argv[5]));
  if(argc!=1) return 1;
  PerformanceSnapshot before{},after{};
  if(perf_snapshot(&before)!=0) return 2;
  int32_t ends[2]; if(pipe(ends)!=0) return 3;
  char read_text[21],write_text[21],deadline_text[21],id_text[kWorkers][21];
  decimal(ends[0],read_text); decimal(ends[1],write_text); decimal(ticks()+30,deadline_text);
  int64_t pids[kWorkers]; bool ok=true;
  for(size_t i=0;i<kWorkers;++i) {
    decimal(i,id_text[i]);
    const char* args[]={argv[0],"worker",id_text[i],read_text,write_text,deadline_text};
    pids[i]=spawn(argv[0],args,6); if(pids[i]<0) ok=false;
  }
  close(ends[1]);
  size_t rounds[kWorkers]{}; uint64_t previous_ticks[kWorkers]{};
  size_t represented=0; bool first_finish=true;
  Progress progress; size_t buffered=0; int64_t count;
  while((count=read(ends[0],reinterpret_cast<uint8_t*>(&progress)+buffered,sizeof(progress)-buffered))>0) {
    buffered+=static_cast<size_t>(count); if(buffered<sizeof(progress)) continue;
    buffered=0;
    if(progress.worker>=kWorkers) { ok=false; continue; }
    const size_t id=progress.worker;
    if(rounds[id]==0) ++represented;
    if(progress.round!=rounds[id]+1 || progress.round>kRounds ||
       progress.ticks<previous_ticks[id] ||
       progress.result!=bench_expected(progress.round*kIterations,id+123)) ok=false;
    rounds[id]=progress.round; previous_ticks[id]=progress.ticks;
    if(progress.round==kRounds && first_finish) {
      if(represented!=kWorkers) ok=false;
      first_finish=false;
    }
  }
  close(ends[0]); if(count<0 || buffered!=0) ok=false;
  for(size_t i=0;i<kWorkers;++i) if(pids[i]>=0) {
    int32_t status=-1;
    if(waitpid(pids[i],&status)!=pids[i] || status!=0 || rounds[i]!=kRounds) ok=false;
  }
  if(perf_snapshot(&after)!=0 || after.preempt_requests<=before.preempt_requests ||
     after.free_pages!=before.free_pages) ok=false;
  print(ok?"sched_test eight_workers_progress_before_first_exit_ok\n":"sched_test: progress, data or resource check failed\n");
  return ok?0:4;
}
