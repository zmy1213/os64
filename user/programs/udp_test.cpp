#include "udp.hpp"
namespace {
bool equal(const char* a,const char* b) { while(*a && *a==*b){++a;++b;}return *a==*b; }
bool decimal(const char* text,uint32_t limit,uint32_t* result) {
  if(!text || !*text)return false;
  uint32_t n=0;
  while(*text){if(*text<'0'||*text>'9')return false;uint32_t digit=*text++-'0';if(n>(limit-digit)/10)return false;n=n*10+digit;}
  if(n==0)return false;*result=n;return true;
}
bool ipv4(const char* text,uint32_t* result) {
  uint32_t address=0;
  for(unsigned i=0;i<4;++i){
    if(*text<'0'||*text>'9')return false;uint32_t byte=0;unsigned digits=0;
    while(*text>='0'&&*text<='9'){byte=byte*10+(*text++-'0');if(++digits>3||byte>255)return false;}
    address=(address<<8)|byte;if(i<3){if(*text++!='.')return false;}else if(*text)return false;
  }
  *result=address;return true;
}
void format_handle(uint32_t handle,char* output) {
  char reverse[11];unsigned count=0;do{reverse[count++]=static_cast<char>('0'+handle%10);handle/=10;}while(handle);
  unsigned index=0;while(count)output[index++]=reverse[--count];output[index]='\0';
}
}
extern "C" int main(int argc,char** argv) {
  if(argc==3 && equal(argv[1],"foreign")) {
    uint32_t handle=0;if(!decimal(argv[2],0x7fffffff,&handle))return 2;
    char byte=0;UdpDatagram metadata{};
    if(udp_close(handle)!=-2 || udp_send(handle,0x0a000202,5151,&byte,1)!=-2 ||
        udp_receive(handle,&metadata,&byte,1)!=-2)return 3;
    print("udp_test foreign_handle_rejected\n");return 0;
  }
  if(argc==2 && equal(argv[1],"leak")) {
    for(unsigned i=0;i<4;++i)if(udp_open(static_cast<uint16_t>(9100+i))<=0)return 3;
    // 故意不 close：内核在进程退出时必须归还全部 socket 槽。
    print("udp_test exit_cleanup_requested\n");return 0;
  }
  uint32_t destination=0,port=0,count=32;
  if((argc!=3 && argc!=4)||!ipv4(argv[1],&destination)||!decimal(argv[2],65535,&port)||
      (argc==4 && !decimal(argv[3],1000,&count))) {
    error("usage: run /bin/udp_test IP PORT [COUNT], with an external UDP echo server\n");return 1;
  }
  const int32_t handle=udp_open(9001);
  if(handle<=0){error("udp_test: socket open failed\n");return 2;}
  // 直接调用 ABI 绕过 wrapper 的窄整数类型，验证内核先拒绝越界参数，
  // 不截断成另一端口/IP，也不在内核中解引用不可访问的用户指针。
  char boundary_byte=0;UdpDatagram boundary_metadata{};
  const uint64_t good_byte=reinterpret_cast<uint64_t>(&boundary_byte);
  const uint64_t good_metadata=reinterpret_cast<uint64_t>(&boundary_metadata);
  if(syscall(36,0x100000001ULL)!=-2 || syscall(37,0x100000000ULL+handle)!=-2 ||
     syscall(38,handle,0x100000000ULL+destination,port,good_byte,1)!=-2 ||
     syscall(38,handle,destination,0x10000,good_byte,1)!=-2 ||
     syscall(38,handle,destination,port,1,1)!=-2 ||
     syscall(38,handle,destination,port,good_byte,1201)!=-2 ||
     syscall(39,handle,1,good_byte,1)!=-2 ||
     syscall(39,handle,good_metadata,1,1)!=-2 ||
     syscall(39,handle,good_metadata,good_byte,1201)!=-2) {
    error("udp_test: invalid user arguments were not rejected\n");udp_close(handle);return 3;
  }
  print("udp_test invalid_pointer_and_range_rejected\n");
  char handle_string[12];format_handle(static_cast<uint32_t>(handle),handle_string);
  const char* child_arguments[]={"/bin/udp_test","foreign",handle_string};
  int64_t child=spawn("/bin/udp_test",child_arguments,3);int32_t child_status=-1;
  if(child<=0||waitpid(child,&child_status)!=child||child_status!=0){udp_close(handle);return 3;}
  // 全部槽用完时必须返回错误，不能越界或覆盖仍在使用的 socket。
  int32_t extra[3];for(unsigned i=0;i<3;++i){extra[i]=udp_open(static_cast<uint16_t>(9002+i));if(extra[i]<=0)return 4;}
  if(udp_open(9010)!=-1)return 4;
  for(int32_t socket:extra)if(udp_close(socket)!=0)return 4;
  unsigned char output[1200],input[1200];
  uint64_t total=0;
  for(uint32_t iteration=0;iteration<count;++iteration) {
    const size_t sizes[4]={0,1,257,1200};const size_t bytes=sizes[iteration%4];
    for(size_t i=0;i<bytes;++i)output[i]=static_cast<unsigned char>(i*37+iteration);
    if(udp_send(handle,destination,static_cast<uint16_t>(port),output,bytes)!=static_cast<int64_t>(bytes)) {
      error("udp_test: send failed\n");udp_close(handle);return 5;
    }
    UdpDatagram metadata{};int64_t received=-1;
    for(unsigned wait=0;wait<300 && received==-1;++wait) {
      received=udp_receive(handle,&metadata,input,sizeof(input));if(received==-1)sleep(1);
    }
    if(received!=static_cast<int64_t>(bytes)||metadata.source_address!=destination||metadata.source_port!=port||
        metadata.destination_port!=9001||metadata.payload_bytes!=bytes) {
      error("udp_test: reply metadata or size mismatch\n");udp_close(handle);return 6;
    }
    for(size_t i=0;i<bytes;++i)if(input[i]!=output[i]){error("udp_test: reply bytes changed\n");udp_close(handle);return 7;}
    total+=bytes;
  }
  if(udp_close(handle)!=0||udp_close(handle)!=-2)return 8;
  print("udp_test sent=");number(count);print(" received=");number(count);print(" payload_bytes=");number(total);print("\n");
  print("udp_test binary_zero_max_payload_ok\n");return 0;
}
