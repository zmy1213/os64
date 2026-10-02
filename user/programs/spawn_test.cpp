#include "os64.hpp"
extern "C" int main(int argc, char**) {
  const char* args[]={"/bin/echo","spawn child ok"};
  int64_t pid=spawn(args[0],args,2);
  if(pid<0) { error("spawn_test: spawn failed\n"); return 1; }
  if(argc>1) { print("spawn_test orphan_started\n"); return 0; }
  int32_t status=-1;
  if(waitpid(pid,&status)!=pid) { error("spawn_test: wait failed\n"); return 2; }
  print("spawn_test child_status="); number(static_cast<uint32_t>(status)); print("\n");
  return status==0?0:3;
}
