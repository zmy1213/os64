#include "os64.hpp"
extern "C" int main(int argc,char**) {
  if(argc!=1) return 1;
  PerformanceSnapshot snapshot{};
  if(perf_snapshot(&snapshot)!=0 || snapshot.abi_version!=1 || snapshot.timer_hz!=100 ||
     (snapshot.cpu_features&15)!=15) return 2;
  if(perf_snapshot(nullptr)!=-1 || syscall(32,0x10000000)!=-1 ||
     syscall(31,0x10000000,1,0)!=-1 || syscall(31,0x400000,1,0)!=-1 ||
     read_log(nullptr,65)!=-1 || read_log(nullptr,0)!=0) return 3;
  KernelLogRecord records[8]; const int64_t count=read_log(records,8);
  if(count<1 || count>8) return 4;
  for(int64_t i=0;i<count;++i) {
    if(records[i].sequence==0 || records[i].reserved!=0 ||
       records[i].component[15]!=0 || records[i].message[87]!=0 ||
       (i>0 && records[i].sequence<=records[i-1].sequence)) return 5;
  }
  if(read_log(records,8,UINT64_MAX)!=0) return 6;
  print("perf_test snapshot_logs_bad_pointers_ok\n"); return 0;
}
