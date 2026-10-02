#include "os64.hpp"
extern "C" int main(int argc,char** argv) {
  if(argc>2) { error("usage: wc [PATH]\n"); return 1; }
  int fd=argc==2?open(argv[1],READ):0;
  if(fd<0) { error("wc: cannot open file\n"); return 1; }
  uint64_t lines=0,words=0,bytes=0;
  bool inside_word=false;
  char buffer[512];
  int64_t count;
  while((count=read(fd,buffer,sizeof(buffer)))>0) {
    bytes+=count;
    for(int64_t i=0;i<count;++i) {
      unsigned char c=buffer[i];
      if(c=='\n') ++lines;
      bool space=c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\v' || c=='\f';
      if(!space && !inside_word) ++words;
      inside_word=!space;
    }
  }
  if(argc==2) close(fd);
  if(count<0) { error("wc: read error\n"); return 1; }
  number(lines); print(" "); number(words); print(" "); number(bytes); print("\n");
  return 0;
}
