#include "os64.hpp"
static char buffer[256];
extern "C" int main(int argc, char** argv) {
  if(argc<2) { error("usage: cat PATH...\n"); return 1; }
  int result=0;
  for(int i=1;i<argc;++i) {
    int fd=open(argv[i]);
    if(fd<0) { error("cat: cannot open "); error(argv[i]); error("\n"); result=1; continue; }
    int64_t count;
    while((count=read(fd,buffer,sizeof(buffer)))>0) if(write(1,buffer,count)!=count) { result=1; break; }
    if(count<0) { error("cat: read error\n"); result=1; }
    close(fd);
  }
  return result;
}
