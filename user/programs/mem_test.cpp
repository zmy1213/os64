#include "os64.hpp"
#include "memory.hpp"

namespace {
constexpr size_t kPage=4096;
// 这个真实的只读查找表令 ELF 文件超过旧的 4 KiB 装载界限。
// 每个字节都在运行时校验，因此它不是为了凑大小而添加的无用填充。
struct Fixture { unsigned char bytes[8192]; };
constexpr Fixture make_fixture() {
  Fixture result{};
  for(size_t i=0;i<sizeof(result.bytes);++i)
    result.bytes[i]=static_cast<unsigned char>((i*37+11)%251);
  return result;
}
const Fixture fixture=make_fixture();

bool raw_break_test() {
  uintptr_t base=brk();
  if(!base || base%kPage) return false;
  uintptr_t end=base+3*kPage+37;
  if(brk(end)!=end) return false;
  auto* bytes=reinterpret_cast<volatile unsigned char*>(base);
  for(size_t i=0;i<end-base;++i) {
    if(bytes[i]!=0) return false;
    bytes[i]=static_cast<unsigned char>(i%251+1);
  }
  // 保留同一页中的前 23 字节。重新增长时，后面的旧数据必须清零。
  if(brk(base+23)!=base+23 || brk(end)!=end) return false;
  for(size_t i=0;i<end-base;++i) {
    unsigned char expected=i<23?static_cast<unsigned char>(i%251+1):0;
    if(bytes[i]!=expected) return false;
  }
  if(brk(base-1)!=end || brk(UINTPTR_MAX)!=end || brk(0x800000)!=end ||
      brk(0x7EF001)!=end || brk(base+(1ULL<<32))!=end)
    return false;
  return brk(base)==base;
}

bool allocator_test() {
  uintptr_t base=brk();
  constexpr size_t big_size=1024*1024+64*1024+13;
  auto* big=static_cast<unsigned char*>(memory::allocate(big_size));
  if(!big || reinterpret_cast<uintptr_t>(big)%16) return false;
  for(size_t i=0;i<big_size;++i) big[i]=static_cast<unsigned char>((i*17+3)%251);
  for(size_t i=0;i<big_size;++i)
    if(big[i]!=static_cast<unsigned char>((i*17+3)%251)) return false;
  void* first=memory::allocate(64);
  void* middle=memory::allocate(512);
  void* tail=memory::allocate(64);
  if(!first || !middle || !tail) return false;
  memory::release(middle);
  void* half=memory::allocate(256);
  void* quarter=memory::allocate(64);
  if(half!=middle || !quarter) return false;  // 分割后的余块确实能够复用。
  memory::release(half);
  memory::release(quarter);
  void* combined=memory::allocate(512);
  if(combined!=middle) return false;          // 相邻洞已经合并。
  memory::release(combined);
  memory::release(tail);
  memory::release(first);
  memory::release(big);
  if(brk()!=base) return false;              // 所有尾部空块真正归还给内核。
  big=static_cast<unsigned char*>(memory::allocate(big_size));
  if(!big) return false;
  for(size_t i=0;i<big_size;++i) if(big[i]!=0) return false;
  memory::release(big);
  if(memory::allocate(0) || memory::allocate(SIZE_MAX)) return false;
  auto* small=static_cast<unsigned char*>(memory::allocate(17));
  if(!small) return false;
  for(size_t i=0;i<17;++i) small[i]=static_cast<unsigned char>(i+1);
  if(memory::resize(small,SIZE_MAX)!=nullptr) return false;
  auto* grown=static_cast<unsigned char*>(memory::resize(small,9000));
  if(!grown) return false;
  for(size_t i=0;i<17;++i) if(grown[i]!=i+1) return false;
  memory::release(grown);
  memory::release(nullptr);
  return brk()==base;
}

bool large_stack_test() {
  // 16 KiB 的局部数组真正落在用户栈上，旧的单页栈无法完成这个测试。
  volatile unsigned char stack[16*1024];
  for(size_t i=0;i<sizeof(stack);++i) stack[i]=static_cast<unsigned char>(i%251);
  for(size_t i=0;i<sizeof(stack);++i) if(stack[i]!=i%251) return false;
  return true;
}

bool seed_size(const char* text, size_t* result) {
  if(!*text) return false;
  size_t value=0;
  while(*text) {
    if(*text<'0' || *text>'9') return false;
    size_t digit=static_cast<size_t>(*text++-'0');
    if(value>(69632-digit)/10) return false;
    value=value*10+digit;
  }
  *result=value;
  return value!=0;
}
}

extern "C" int main(int argc, char** argv) {
  const volatile unsigned char* table=fixture.bytes;
  for(size_t i=0;i<sizeof(fixture.bytes);++i)
    if(table[i]!=static_cast<unsigned char>((i*37+11)%251)) {
      error("mem_test: large ELF data mismatch\n"); return 1;
    }
  if(!large_stack_test()) { error("mem_test: stack mismatch\n"); return 2; }
  if(!raw_break_test()) { error("mem_test: brk shrink/zero/reject failed\n"); return 3; }
  if(!allocator_test()) { error("mem_test: allocator failed\n"); return 4; }
  print("mem_test large_elf_ok\n"
        "mem_test large_stack_ok\n"
        "mem_test brk_zero_reject_ok\n"
        "mem_test heap_1m_reuse_ok\n");
  // 为编辑器回归准备一个超过 32 KiB 的纯文本文件；普通运行不会写磁盘。
  if(argc>1) {
    if(argc!=4 || argv[1][0]!='s' || argv[1][1]!='e' || argv[1][2]!='e' ||
        argv[1][3]!='d' || argv[1][4]!='\0') {
      error("usage: run /bin/mem_test [seed PATH BYTES]\n"); return 5;
    }
    size_t size=0;
    if(!seed_size(argv[3],&size)) { error("mem_test: invalid seed size\n"); return 5; }
    auto* text=static_cast<char*>(memory::allocate(size));
    if(!text) return 6;
    for(size_t i=0;i<size;++i) text[i]=static_cast<char>('a'+i%26);
    int64_t written=replace_file(argv[2],text,size);
    memory::release(text);
    if(written!=static_cast<int64_t>(size) || sync()!=0) {
      error("mem_test: seed failed\n"); return 6;
    }
    print("mem_test seeded "); number(size); print(" bytes\n");
  }
  return 0;
}
