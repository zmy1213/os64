#include "os64.hpp"
extern "C" int main(int, char**) {
  print("fault: testing isolated invalid memory access\n");
  *reinterpret_cast<volatile uint64_t*>(0x10000000)=42;
  return 1;
}
