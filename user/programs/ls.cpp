#include "os64.hpp"
static DirectoryEntry entries[128];
extern "C" int main(int argc, char** argv) {
  const char* path=argc>1?argv[1]:".";
  int64_t count=listdir(path,entries,128);
  if(count<0) { error("ls: cannot list directory\n"); return 1; }
  for(int64_t i=0;i<count;++i) {
    print(entries[i].name); if(entries[i].type==2) print("/");
    print(" "); number(entries[i].size_bytes); print("\n");
  }
  return 0;
}
