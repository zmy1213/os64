#include "os64.hpp"
static char buffer[128];
extern "C" int main(int argc, char** argv) {
  const char* path=argc>1?argv[1]:"/writer.txt";
  const char* text=argc>2?argv[2]:"userland persistent file\n";
  int fd=open(path,READ|WRITE|CREATE|TRUNC);
  if(fd<0) { error("writer: open failed\n"); return 1; }
  bool ok=write(fd,text,length(text))==static_cast<int64_t>(length(text));
  ok=close(fd)==0 && ok;
  ok=sync()==0 && ok;
  if(!ok) { error("writer: write/sync failed\n"); return 2; }
  fd=open(path);
  if(fd<0) return 3;
  int64_t count=read(fd,buffer,sizeof(buffer));
  if(count<0) { close(fd); return 4; }
  print("writer readback="); write(1,buffer,count); print("\n"); close(fd);
  return 0;
}
