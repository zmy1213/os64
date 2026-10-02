#include "os64.hpp"
extern "C" int main(int argc, char** argv) {
  if(argc!=2) { error("usage: mkdir PATH\n"); return 1; }
  if(syscall(12,reinterpret_cast<uint64_t>(argv[1]))<0) { error("mkdir: failed\n"); return 2; }
  return 0;
}
