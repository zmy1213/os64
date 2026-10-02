#include "os64.hpp"
extern "C" int main(int argc, char** argv) {
  uint64_t ms=argc>1?parse_number(argv[1]):100;
  print("sleep started\n");
  if(sleep(ms)<0) return 1;
  print("sleep finished\n"); return 0;
}
