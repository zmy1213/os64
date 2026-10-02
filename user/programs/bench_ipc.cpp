#if defined(OS64_LINUX_BENCH)
#include "linux_adapter.hpp"
#else
#include "os64.hpp"
#endif
namespace {
bool equal(const char* a,const char* b) { while(*a && *a==*b) { ++a; ++b; } return *a==*b; }
bool integer(const char* text,uint64_t* output) {
  uint64_t value=0; if(!*text) return false;
  while(*text) { if(*text<'0' || *text>'9' || value>(UINT64_MAX-(*text-'0'))/10) return false;
    value=value*10+(*text++-'0'); }
  *output=value; return true;
}
void decimal(uint64_t value,char* output) {
  char digits[21]; size_t n=0;
  do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(n) output[i++]=digits[--n]; output[i]=0;
}
void field(const char* label,uint64_t value) { print(label); number(value); print("\n"); }
int reader(int read_fd,int write_fd,uint64_t bytes) {
  if(close(write_fd)!=0) return 10;
  unsigned char buffer[4096]; uint64_t offset=0;
  int64_t count;
  while((count=read(read_fd,buffer,sizeof(buffer)))>0) {
    for(int64_t i=0;i<count;++i) if(buffer[i]!=static_cast<unsigned char>(offset+i)) return 11;
    offset+=count;
  }
  close(read_fd);
  return count==0 && offset==bytes?0:12;
}
}
extern "C" int main(int argc,char** argv) {
  uint64_t bytes=32*1024*1024;
  if(argc==5 && equal(argv[1],"reader")) {
    uint64_t read_fd,write_fd;
    if(!integer(argv[2],&read_fd)||!integer(argv[3],&write_fd)||!integer(argv[4],&bytes)) return 13;
    return reader(static_cast<int>(read_fd),static_cast<int>(write_fd),bytes);
  }
  if(argc>2 || (argc==2 && !integer(argv[1],&bytes)) || bytes<1 || bytes>64*1024*1024) {
    error("usage: run /bin/bench_ipc [bytes 1..67108864]\n"); return 1;
  }
  unsigned char chunk[4096];
  for(size_t i=0;i<sizeof(chunk);++i) chunk[i]=static_cast<unsigned char>(i);
  int32_t ends[2]; char read_text[21],write_text[21],size_text[21];
  PerformanceSnapshot before{},after{};
  if(perf_snapshot(&before)!=0 || pipe(ends)!=0) return 2;
  decimal(ends[0],read_text); decimal(ends[1],write_text); decimal(bytes,size_text);
  const char* arguments[]={argv[0],"reader",read_text,write_text,size_text,nullptr};
  const int64_t pid=spawn(argv[0],arguments,5);
  if(pid<0) { close(ends[0]); close(ends[1]); return 3; }
  close(ends[0]);
  uint64_t offset=0; bool ok=true;
  while(offset<bytes) {
    const size_t count=bytes-offset<sizeof(chunk)?bytes-offset:sizeof(chunk);
    size_t written=0;
    while(written<count) {
      const int64_t n=write(ends[1],chunk+written,count-written);
      if(n<=0) { ok=false; break; }
      written+=static_cast<size_t>(n);
    }
    if(!ok) break;
    offset+=count;
  }
  close(ends[1]); int32_t status=-1;
  if(waitpid(pid,&status)!=pid || status!=0) ok=false;
  if(perf_snapshot(&after)!=0) return 4;
  field("ipc bytes=",bytes);
  field("ipc pipe_capacity=",4096);
  field("ipc elapsed_ticks=",after.ticks-before.ticks);
  field("ipc timer_hz=",after.timer_hz);
  field("ipc context_switches=",after.context_switches-before.context_switches);
  field("ipc free_pages_before=",before.free_pages);
  field("ipc free_pages_after=",after.free_pages);
  print(ok?"ipc correctness=ok\n":"ipc correctness=FAILED\n");
  return ok?0:5;
}
