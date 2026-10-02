#include "os64.hpp"
extern "C" int main(int argc, char** argv) {
  uint64_t iterations=argc>1?parse_number(argv[1]):10000000;
  print("spin started\n");
  volatile uint64_t count=0;
  while(count<iterations) ++count;
  print("spin finished\n"); return 0;
}
