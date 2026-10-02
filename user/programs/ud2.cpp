#include "os64.hpp"
extern "C" int main(int, char**) {
  print("ud2: testing isolated invalid opcode\n");
  asm volatile("ud2");
  return 1;
}
