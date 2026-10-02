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
  print("badptr flags rejected\n");
  // replace_file 也必须经过用户边界校验，不能让坏参数进入磁盘事务。
  result=replace_file(reinterpret_cast<const char*>(0x10000000),"safe",4);
  if(result!=-1) return 4;
  print("badptr replace path rejected\n");
  result=replace_file("/readme.txt",reinterpret_cast<const void*>(0x10000000),32);
  if(result!=-1) return 5;
  result=replace_file("/readme.txt",nullptr,1);
  if(result!=-1) return 6;
  result=replace_file("/readme.txt",reinterpret_cast<const void*>(UINTPTR_MAX-7),16);
  if(result!=-1) return 7;
  print("badptr replace buffer rejected\n");
  result=replace_file("/readme.txt","safe",69633);
  if(result!=-1) return 8;
  result=replace_file("/readme.txt","safe",(1ULL<<32)|4);
  if(result!=-1) return 9;
  print("badptr replace size rejected\n");
  return 0;
}
