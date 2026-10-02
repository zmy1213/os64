#include "memory.hpp"
#include "os64.hpp"

extern "C" void* memcpy(void* destination, const void* source, size_t bytes) {
  auto* out=static_cast<unsigned char*>(destination);
  const auto* in=static_cast<const unsigned char*>(source);
  for(size_t i=0;i<bytes;++i) out[i]=in[i];
  return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t bytes) {
  auto* out=static_cast<unsigned char*>(destination);
  const auto* in=static_cast<const unsigned char*>(source);
  // 重叠时方向很重要：把 abcde 向右移一字节，必须从 e 开始复制。
  if(reinterpret_cast<uintptr_t>(out)<reinterpret_cast<uintptr_t>(in)) {
    for(size_t i=0;i<bytes;++i) out[i]=in[i];
  } else {
    while(bytes) { --bytes; out[bytes]=in[bytes]; }
  }
  return destination;
}
extern "C" void* memset(void* destination, int value, size_t bytes) {
  auto* out=static_cast<unsigned char*>(destination);
  for(size_t i=0;i<bytes;++i) out[i]=static_cast<unsigned char>(value);
  return destination;
}
extern "C" int memcmp(const void* left, const void* right, size_t bytes) {
  const auto* a=static_cast<const unsigned char*>(left);
  const auto* b=static_cast<const unsigned char*>(right);
  for(size_t i=0;i<bytes;++i) if(a[i]!=b[i]) return a[i]<b[i]?-1:1;
  return 0;
}

namespace memory {
namespace {
constexpr size_t kAlignment=16;
// 每块内存前面藏一小段“账本”。用户收到的是账本后面的 payload。
// 32 字节的账本加上 16 字节对齐的 payload，保证下一块也对齐。
struct alignas(kAlignment) Block {
  size_t bytes;
  Block* previous;
  Block* next;
  bool available;
};
static_assert(sizeof(Block)==32,"block headers must preserve 16-byte alignment");
Block* first=nullptr;
Block* last=nullptr;

bool round_size(size_t bytes, size_t* rounded) {
  if(bytes==0 || bytes>SIZE_MAX-(kAlignment-1)) return false;
  *rounded=(bytes+kAlignment-1)&~(kAlignment-1);
  return *rounded<=SIZE_MAX-sizeof(Block);
}

void split(Block* block, size_t wanted) {
  // 剩余空间要同时容得下下一块账本和至少 16 字节 payload，才值得分割。
  if(block->bytes-wanted<sizeof(Block)+kAlignment) return;
  auto* remainder=reinterpret_cast<Block*>(
      reinterpret_cast<unsigned char*>(block+1)+wanted);
  remainder->bytes=block->bytes-wanted-sizeof(Block);
  remainder->available=true;
  remainder->previous=block;
  remainder->next=block->next;
  if(remainder->next) remainder->next->previous=remainder;
  else last=remainder;
  block->next=remainder;
  block->bytes=wanted;
}

void merge_next(Block* block) {
  Block* next=block->next;
  if(!next || !next->available) return;
  block->bytes+=sizeof(Block)+next->bytes;
  block->next=next->next;
  if(block->next) block->next->previous=block;
  else last=block;
}
}

void* allocate(size_t bytes) {
  size_t wanted=0;
  if(!round_size(bytes,&wanted)) return nullptr;
  for(Block* block=first;block;block=block->next) {
    if(block->available && block->bytes>=wanted) {
      split(block,wanted);
      block->available=false;
      return block+1;
    }
  }
  // 没有可复用的洞：向内核申请扩大堆。内核按页分配，我们按字节记账。
  uintptr_t old_end=brk();
  if(old_end==0 || old_end%kAlignment!=0 || old_end>UINTPTR_MAX-sizeof(Block) ||
      wanted>UINTPTR_MAX-old_end-sizeof(Block)) return nullptr;
  uintptr_t new_end=old_end+sizeof(Block)+wanted;
  if(brk(new_end)!=new_end) return nullptr;
  auto* block=reinterpret_cast<Block*>(old_end);
  block->bytes=wanted;
  block->previous=last;
  block->next=nullptr;
  block->available=false;
  if(last) last->next=block;
  else first=block;
  last=block;
  return block+1;
}

void release(void* pointer) {
  if(!pointer) return;
  Block* block=static_cast<Block*>(pointer)-1;
  block->available=true;
  merge_next(block);
  if(block->previous && block->previous->available) {
    block=block->previous;
    merge_next(block);
  }
  // 堆中间的洞只能复用；堆末尾的空块可以真正归还给内核。
  // 缩堆后 block 的页面可能消失，因此先保存 previous，再改链表。
  if(block==last) {
    Block* previous=block->previous;
    uintptr_t target=reinterpret_cast<uintptr_t>(block);
    if(brk(target)==target) {
      last=previous;
      if(previous) previous->next=nullptr;
      else first=nullptr;
    }
  }
}

void* resize(void* pointer, size_t bytes) {
  if(!pointer) return allocate(bytes);
  if(bytes==0) { release(pointer); return nullptr; }
  size_t wanted=0;
  if(!round_size(bytes,&wanted)) return nullptr;
  Block* block=static_cast<Block*>(pointer)-1;
  if(block->bytes>=wanted) {
    // 保留原块有助于反复编辑时复用容量；这里不缩小账本中的尺寸。
    return pointer;
  }
  if(block->next && block->next->available &&
      block->bytes+sizeof(Block)+block->next->bytes>=wanted) {
    merge_next(block);
    split(block,wanted);
    return pointer;
  }
  void* replacement=allocate(bytes);
  if(!replacement) return nullptr;
  memcpy(replacement,pointer,block->bytes);
  release(pointer);
  return replacement;
}
}
