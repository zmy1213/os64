#include "os64.hpp"
static char buffer[11*512];
extern "C" int main(int argc, char** argv) {
  const char* path=argc>1?argv[1]:"/large.txt";
  bool verify_only=argc>2;
  if(!verify_only) {
    for(size_t i=0;i<sizeof(buffer);++i) buffer[i]=static_cast<char>('A'+i/512);
    int fd=open(path,READ|WRITE|CREATE|TRUNC);
    if(fd<0) { error("fs_test: create failed\n"); return 1; }
    int64_t count=write(fd,buffer,sizeof(buffer));
    close(fd);
    if(count!=sizeof(buffer)||sync()<0) { error("fs_test: write failed\n"); return 2; }
  }
  for(size_t i=0;i<sizeof(buffer);++i) buffer[i]=0;
  int fd=open(path);
  if(fd<0) return 3;
  int64_t count=read(fd,buffer,sizeof(buffer));
  close(fd);
  if(count!=sizeof(buffer)) { error("fs_test: size mismatch\n"); return 4; }
  for(size_t i=0;i<sizeof(buffer);++i) if(buffer[i]!=static_cast<char>('A'+i/512)) {
    error("fs_test: data mismatch\n"); return 5;
  }
  print("fs_test direct_indirect_ok\n"); return 0;
}
