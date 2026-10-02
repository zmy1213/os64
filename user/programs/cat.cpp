#include "os64.hpp"
static char buffer[256];
extern "C" int main(int argc, char** argv) {
  if(argc<2) {
    // 没有路径时，复制 stdin 到 stdout；Shell 可把它们接到管道或文件。
    int64_t count;
    while((count=read(0,buffer,sizeof(buffer)))>0)
      if(write(1,buffer,count)!=count) { error("cat: write error\n"); return 1; }
    if(count<0) { error("cat: read error\n"); return 1; }
    return 0;
  }
  int result=0;
  for(int i=1;i<argc;++i) {
    int fd=open(argv[i]);
    if(fd<0) { error("cat: not found or cannot open "); error(argv[i]); error("\n"); result=1; continue; }
    int64_t count;
    while((count=read(fd,buffer,sizeof(buffer)))>0) if(write(1,buffer,count)!=count) { result=1; break; }
    if(count<0) { error("cat: read error\n"); result=1; }
    close(fd);
  }
  return result;
}
