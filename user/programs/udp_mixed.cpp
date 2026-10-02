#include "udp.hpp"
#include "smp.hpp"
#include "bench_workload.hpp"

namespace {
bool equal(const char* a,const char* b) { while(*a && *a==*b){++a;++b;}return *a==*b; }
bool integer(const char* text,uint64_t limit,uint64_t* out) {
  if(!text || !*text)return false;uint64_t value=0;
  while(*text) {
    if(*text<'0'||*text>'9')return false;
    const uint64_t digit=static_cast<unsigned>(*text++-'0');
    if(value>limit/10 || (value==limit/10 && digit>limit%10))return false;
    value=value*10+digit;
  }
  *out=value;return true;
}
bool ipv4(const char* text,uint32_t* out) {
  uint32_t value=0;
  for(unsigned i=0;i<4;++i) {
    if(*text<'0'||*text>'9')return false;uint32_t byte=0;unsigned digits=0;
    while(*text>='0' && *text<='9'){byte=byte*10+static_cast<unsigned>(*text++-'0');if(++digits>3||byte>255)return false;}
    value=(value<<8)|byte;if(i<3){if(*text++!='.')return false;}else if(*text)return false;
  }
  *out=value;return true;
}
void decimal(uint64_t value,char* output) {
  char reverse[21];size_t count=0;do{reverse[count++]=static_cast<char>('0'+value%10);value/=10;}while(value);
  size_t i=0;while(count)output[i++]=reverse[--count];output[i]='\0';
}
void field(const char* name,uint64_t value){print(name);number(value);print("\n");}
int receiver(uint32_t destination,uint16_t port,uint64_t count,int input_fd,int output_fd) {
  // 父进程先创建所有计算者，再写一字节放行。子进程继承 pipe 后必须关闭自己
  // 不使用的写端，否则父进程取消时，自己的写端会让 read 永远等不到 EOF。
  close(output_fd);char start=0;
  const int64_t signal=read(input_fd,&start,1);close(input_fd);
  if(signal!=1 || start!='g')return 3;
  const int32_t socket=udp_open(9200);if(socket<=0)return 2;
  uint8_t sent[1200],received[1200];uint64_t bytes_total=0,packets=0,packets_sent=0;
  bool ok=true;
  for(uint64_t iteration=0;ok && iteration<count;++iteration) {
    const size_t sizes[4]={0,1,257,1200};const size_t bytes=sizes[iteration%4];
    for(size_t i=0;i<bytes;++i)sent[i]=static_cast<uint8_t>(i*37+iteration);
    if(udp_send(socket,destination,port,sent,bytes)!=static_cast<int64_t>(bytes)){ok=false;break;}
    ++packets_sent;
    UdpDatagram metadata{};
    // 同时验证小输出保留完整包、零字节包、无限等待与有限 timeout。
    if(bytes==1200 && udp_receive_wait(socket,&metadata,received,1,2000)!=-2){ok=false;break;}
    const int64_t actual=udp_receive_wait(socket,&metadata,bytes?received:nullptr,bytes?sizeof(received):0,
                                         iteration==1?UINT32_MAX:2000);
    if(actual!=static_cast<int64_t>(bytes) || metadata.source_address!=destination ||
       metadata.source_port!=port || metadata.destination_port!=9200 || metadata.payload_bytes!=bytes || metadata.reserved!=0){ok=false;break;}
    for(size_t i=0;i<bytes;++i)if(received[i]!=sent[i]){ok=false;break;}
    if(ok){bytes_total+=bytes;++packets;}
  }
  SmpSnapshot snapshot{};
  if(udp_close(socket)!=0 || smp_snapshot(&snapshot)!=0)ok=false;
  field("udp_mixed sent=",packets_sent);field("udp_mixed received=",packets);
  field("udp_mixed payload_bytes=",bytes_total);field("udp_mixed receiving_cpu=",snapshot.current_cpu);
  print(ok?"udp_mixed receiver=ok\n":"udp_mixed receiver=FAILED\n");return ok?0:4;
}
}
extern "C" int main(int argc,char** argv) {
  if(argc==7 && equal(argv[1],"receiver")) {
    uint32_t destination=0;uint64_t port=0,count=0,input=0,output=0;
    if(!ipv4(argv[2],&destination) || !integer(argv[3],65535,&port) || !port ||
       !integer(argv[4],1000,&count) || !count || !integer(argv[5],15,&input) ||
       !integer(argv[6],15,&output) || input==output)return 1;
    return receiver(destination,static_cast<uint16_t>(port),count,static_cast<int>(input),static_cast<int>(output));
  }
  uint32_t destination=0;uint64_t port=0,workers=3,count=64,iterations=100000000;
  if(argc<3 || argc>6 || !ipv4(argv[1],&destination) || !integer(argv[2],65535,&port) || !port ||
     (argc>3 && !integer(argv[3],3,&workers)) || (argc>4 && !integer(argv[4],1000,&count)) || !count ||
     (argc>5 && !integer(argv[5],500000000,&iterations)) || !iterations) {
    error("usage: run /bin/udp_mixed IP PORT [workers 0..3] [packets 1..1000] [iterations 1..500000000]\n");return 1;
  }
  SmpSnapshot before{},after{};PerformanceSnapshot perf_before{},perf_after{};
  if(smp_snapshot(&before)!=0 || before.abi_version!=1 || perf_snapshot(&perf_before)!=0)return 3;
  int32_t gate[2];if(pipe(gate)!=0)return 2;
  char packets_text[21],input_text[21],output_text[21];
  decimal(count,packets_text);decimal(gate[0],input_text);decimal(gate[1],output_text);
  const char* receiver_arguments[]={"/bin/udp_mixed","receiver",argv[1],argv[2],packets_text,input_text,output_text};
  // 网络接收者首先入队：父进程已经 pin 到一颗 CPU，在多核时接收者会选
  // 另一颗空闲 CPU。它只等待共享 inbox，RX ring 仍由 BSP worker 独占。
  const int64_t network_child=spawn("/bin/udp_mixed",receiver_arguments,7);
  if(network_child<=0){close(gate[0]);close(gate[1]);return 2;}
  char count_text[21],seed_text[3][21];decimal(iterations,count_text);
  int64_t children[3]={-1,-1,-1};uint32_t expected[3]{};bool ok=true;
  for(size_t i=0;i<workers;++i) {
    const uint64_t seed=123456789+i;decimal(seed,seed_text[i]);
    expected[i]=bench_checksum(bench_expected(iterations,seed));
    const char* arguments[]={"/bin/bench","worker",count_text,seed_text[i],"0"};
    children[i]=spawn("/bin/bench",arguments,5);if(children[i]<0){ok=false;break;}
  }
  close(gate[0]);const char start='g';
  if(ok && write(gate[1],&start,1)!=1)ok=false;
  // 无论成功还是失败，都 waitpid 已创建的计算者，避免下一次实验混入旧负载。
  // 失败时先回收计算者的继承写端，再关最后一个写端，让接收者看到 EOF。
  uint64_t checksum=0;
  for(size_t i=0;i<workers;++i)if(children[i]>0) {
    int32_t status=-1;if(waitpid(children[i],&status)!=children[i] || status!=static_cast<int32_t>(expected[i]))ok=false;
    checksum+=static_cast<uint32_t>(status);
  }
  close(gate[1]);int32_t network_status=-1;
  if(waitpid(network_child,&network_status)!=network_child || network_status!=0)ok=false;
  if(smp_snapshot(&after)!=0 || perf_snapshot(&perf_after)!=0)ok=false;
  field("udp_mixed compute_workers=",workers);field("udp_mixed iterations_per_worker=",iterations);
  field("udp_mixed checksum8_sum=",checksum);field("udp_mixed elapsed_ticks=",perf_after.ticks-perf_before.ticks);
  field("udp_mixed context_switches=",perf_after.context_switches-perf_before.context_switches);
  field("udp_mixed online_cpus=",after.online_cpus);
  for(size_t cpu=0;cpu<4;++cpu) {
    print("udp_mixed cpu=");number(cpu);print(" user_dispatches=");number(after.user_dispatches[cpu]-before.user_dispatches[cpu]);
    print(" user_ticks=");number(after.user_ticks[cpu]-before.user_ticks[cpu]);print("\n");
  }
  print(ok?"udp_mixed correctness=ok\n":"udp_mixed correctness=FAILED\n");return ok?0:4;
}
