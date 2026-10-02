#include "os64.hpp"
extern "C" int main(int, char**) {
  print("stackfault: testing unmapped stack guard\n");
  // 用户栈从 0x7F0000 开始，下面的 0x7EF000 页故意不映射。
  *reinterpret_cast<volatile unsigned char*>(0x7EF000)=1;
  error("stackfault: guard was writable\n");
  return 1;
}
