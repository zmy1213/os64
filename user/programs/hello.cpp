#include "os64.hpp"
extern "C" int main(int argc, char** argv) {
  print("hello from os64 userland\n");
  print("argc="); number(argc); print("\n");
  for(int i=0;i<argc;++i) { print("argv["); number(i); print("]="); print(argv[i]); print("\n"); }
  return 0;
}
