#include "os64.hpp"
extern "C" int main(int, char**) {
  int64_t result=write(1,reinterpret_cast<const void*>(0x10000000),32);
  if(result>=0) { error("badptr: invalid pointer was accepted\n"); return 1; }
  print("badptr rejected\n");
  result=open(reinterpret_cast<const char*>(0x10000000));
  if(result>=0) { close(result); return 2; }
  print("badptr path rejected\n");
  result=open("/readme.txt",(1ULL<<32)|READ);
  if(result!=-1) { if(result>=0) close(result); return 3; }
  print("badptr flags rejected\n"); return 0;
}
