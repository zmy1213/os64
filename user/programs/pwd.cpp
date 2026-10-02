#include "os64.hpp"
static char path[64];
extern "C" int main(int, char**) {
  if(syscall(0,reinterpret_cast<uint64_t>(path),sizeof(path))<0) return 1;
  print(path); print("\n"); return 0;
}
