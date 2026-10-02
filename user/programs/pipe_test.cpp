#include "os64.hpp"
namespace {
bool equal(const char* a,const char* b) { while(*a && *a==*b) { ++a; ++b; } return *a==*b; }
void decimal(int value,char out[12]) {
  char digits[12]; size_t n=0;
  do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
  size_t i=0; while(n) out[i++]=digits[--n]; out[i]=0;
}
constexpr size_t kTransfer=32*1024+17;
constexpr size_t kRecordBytes=257;
constexpr size_t kRecordCount=64;
int write_records(int read_fd,int write_fd,unsigned char id) {
  close(read_fd);
  unsigned char record[kRecordBytes];
  for(size_t sequence=0;sequence<kRecordCount;++sequence) {
    record[0]=id; record[1]=static_cast<unsigned char>(sequence);
    for(size_t i=2;i<kRecordBytes;++i) record[i]=static_cast<unsigned char>((i*17+id)%251);
    if(write(write_fd,record,sizeof(record))!=sizeof(record)) return 1;
  }
  close(write_fd); return 0;
}
int child_reader(int read_fd,int write_fd) {
  close(write_fd); // 子进程不能留下自己用不到的写端，否则永远读不到 EOF。
  char buffer[777]; size_t offset=0; int64_t count;
  while((count=read(read_fd,buffer,sizeof(buffer)))>0) {
    for(int64_t i=0;i<count;++i)
      if(static_cast<unsigned char>(buffer[i])!=static_cast<unsigned char>((offset+i)%251+1)) return 1;
    offset+=count;
  }
  close(read_fd);
  return count==0 && offset==kTransfer?0:2;
}
int spawn_child(const char* mode,int a,int b) {
  char first[12],second[12]; decimal(a,first); decimal(b,second);
  const char* args[]={"/bin/pipe_test",mode,first,second};
  return spawn(args[0],args,4);
}
bool await(int pid) { int32_t status=-1; return waitpid(pid,&status)==pid && status==0; }
}
extern "C" int main(int argc,char** argv) {
  if(argc==4) {
    int a=parse_number(argv[2]),b=parse_number(argv[3]);
    if(equal(argv[1],"reader")) return child_reader(a,b);
    if(equal(argv[1],"offset")) {
      char text[4];
      return read(a,text,sizeof(text))==4 && text[0]=='f' && text[1]=='s' && text[2]==' ' && text[3]=='r'?0:3;
    }
    if(equal(argv[1],"exitwriter")) { close(a); sleep(20); return 0; }
    if(equal(argv[1],"recordA")) return write_records(a,b,'A');
    if(equal(argv[1],"recordB")) return write_records(a,b,'B');
    return 4;
  }
  int32_t ends[2];
  for(size_t repeat=0;repeat<32;++repeat) {
    if(pipe(ends)!=0 || close(ends[0])!=0 || write(ends[1],"x",1)!=BROKEN_PIPE || close(ends[1])!=0) return 5;
  }
  if(pipe(ends)!=0 || write(ends[1],"abc",3)!=3 || close(ends[1])!=0) return 6;
  char small[4];
  if(read(ends[0],small,4)!=3 || small[0]!='a' || small[2]!='c' || read(ends[0],small,4)!=0 || close(ends[0])!=0) return 7;
  print("pipe_test eof_epipe_ok\n");
  if(pipe(ends)!=0) return 8;
  int reader=spawn_child("reader",ends[0],ends[1]);
  if(reader<0) return 9;
  close(ends[0]);
  char chunk[1024]; size_t offset=0;
  while(offset<kTransfer) {
    size_t count=kTransfer-offset<sizeof(chunk)?kTransfer-offset:sizeof(chunk);
    for(size_t i=0;i<count;++i) chunk[i]=static_cast<char>((offset+i)%251+1);
    if(write(ends[1],chunk,count)!=static_cast<int64_t>(count)) return 10;
    offset+=count;
  }
  close(ends[1]);
  if(!await(reader)) return 11;
  print("pipe_test blocking_32k_ok\n");
  // 父进程读空管道会阻塞，直到子进程退出自动关闭最后一个写端。
  if(pipe(ends)!=0) return 12;
  int writer=spawn_child("exitwriter",ends[0],ends[1]);
  if(writer<0) return 13;
  close(ends[1]);
  if(read(ends[0],small,sizeof(small))!=0 || close(ends[0])!=0 || !await(writer)) return 14;
  print("pipe_test exit_wakes_eof_ok\n");
  int file=open("/readme.txt",READ); int alias=dup(file);
  if(file<0 || alias<0 || read(file,small,4)!=4) return 15;
  int child=spawn_child("offset",file,alias);
  if(child<0 || !await(child) || read(alias,small,1)!=1 || small[0]!='e') return 16;
  close(file); close(alias);
  print("pipe_test inherited_offset_ok\n");
  // 两个生产者争同一管道。257 不整除 4096，读者每次取 113 字节，
  // 强迫小写入等待整块空间，也覆盖记录跨环形队列末尾的情况。
  if(pipe(ends)!=0) return 19;
  int writer_a=spawn_child("recordA",ends[0],ends[1]);
  int writer_b=spawn_child("recordB",ends[0],ends[1]);
  if(writer_a<0 || writer_b<0) return 20;
  close(ends[1]);
  unsigned char record_chunk[113];
  size_t position=0,sequences[2]={0,0};
  unsigned char id=0;
  int64_t received;
  while((received=read(ends[0],record_chunk,sizeof(record_chunk)))>0) {
    for(int64_t i=0;i<received;++i) {
      unsigned char byte=record_chunk[i];
      if(position==0) {
        if(byte!='A' && byte!='B') return 21;
        id=byte;
      } else if(position==1) {
        if(byte!=sequences[id-'A']) return 22;
      } else if(byte!=static_cast<unsigned char>((position*17+id)%251)) return 23;
      if(++position==kRecordBytes) { position=0; ++sequences[id-'A']; }
    }
  }
  close(ends[0]);
  if(received!=0 || position || sequences[0]!=kRecordCount || sequences[1]!=kRecordCount ||
      !await(writer_a) || !await(writer_b)) return 24;
  print("pipe_test multiwriter_atomic_ok\n");
  int saved=dup(1);
  if(saved<0 || dup2(saved,1)!=1 || dup2(1,1)!=1 || close(saved)!=0) return 17;
  if(syscall(22,0x10000000)!=-1 || dup2(18,19)!=-1 || dup(-1)!=-1) return 18;
  print("pipe_test dup_validation_ok\n");
  return 0;
}
