#include "os64.hpp"
// 这段字节位于可写数据段，不在 ELF 中标记为可执行的代码段。
static volatile unsigned char code[16]={0xc3};
extern "C" int main(int, char**) {
  // volatile + 运行时写入很重要：否则优化器可把“从未修改的数组”放入只读代码段。
  code[0]=0xc3;
  print("nxfault: testing non-executable data\n");
  // x86 的 ret 指令：若数据页可以执行，这个调用会返回，说明保护失败。
  reinterpret_cast<void (*)()>(const_cast<unsigned char*>(code))();
  error("nxfault: data was executable\n");
  return 2;
}
