#include "fs/fd.hpp"
#include "runtime/runtime.hpp"
#include "memory/kmemory.hpp"
#include "interrupts/interrupts.hpp"
#include "task/scheduler.hpp"

// 固定容量环形队列：head 指向下个待读字节，count 是有效字节数。
struct PipeState {
  uint8_t bytes[kPipeBufferBytes];
  size_t head, count;
  uint32_t readers, writers;
  ThreadControlBlock* read_waiters[kSchedulerMaxThreadCount];
  ThreadControlBlock* write_waiters[kSchedulerMaxThreadCount];
};
namespace {
uint64_t save_interrupt_flags_and_disable() {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  return flags;
}
void restore_interrupt_flags(uint64_t flags) {
  asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}
bool valid(int32_t fd) { return fd>=0 && static_cast<size_t>(fd)<kFileDescriptorSlotCapacity; }
bool structural(const FileDescriptorTable* t) { return t && t->vfs; }
OpenFileDescription* description(const FileDescriptorTable* t, int32_t fd) {
  return structural(t) && valid(fd) && t->entries[fd].open?t->entries[fd].description:nullptr;
}
int32_t empty_slot(const FileDescriptorTable* t) {
  for(size_t i=0;i<kFileDescriptorCapacity;++i) if(!t->entries[i].open) return i;
  return -1;
}
void wake(ThreadControlBlock** waiters) {
  for(size_t i=0;i<kSchedulerMaxThreadCount;++i) {
    auto* thread=waiters[i]; waiters[i]=nullptr;
    if(thread) (void)scheduler_wake_thread(thread);
  }
}
bool wait_on(ThreadControlBlock** waiters) {
  auto* thread=scheduler_active_thread();
  if(!thread || thread->is_idle_thread) return false;
  size_t slot=0;
  while(slot<kSchedulerMaxThreadCount && waiters[slot] && waiters[slot]!=thread) ++slot;
  if(slot==kSchedulerMaxThreadCount) return false;
  waiters[slot]=thread;
  // 调用者已经关中断；登记与睡眠之间不能让生产者插进来，否则可能错过唤醒。
  bool result=scheduler_block_current_thread_and_enable_interrupts();
  if(waiters[slot]==thread) waiters[slot]=nullptr;
  return result;
}
int32_t pipe_read(PipeState* pipe, void* buffer, size_t size) {
  if(!size) return 0;
  for(;;) {
    uint64_t flags=save_interrupt_flags_and_disable();
    if(pipe->count) {
      size_t count=size<pipe->count?size:pipe->count;
      auto* out=static_cast<uint8_t*>(buffer);
      size_t first=count<kPipeBufferBytes-pipe->head?count:kPipeBufferBytes-pipe->head;
      memory_copy(out,pipe->bytes+pipe->head,first);
      memory_copy(out+first,pipe->bytes,count-first);
      pipe->head=(pipe->head+count)%kPipeBufferBytes;
      pipe->count-=count;
      wake(pipe->write_waiters);
      restore_interrupt_flags(flags);
      return static_cast<int32_t>(count);
    }
    if(!pipe->writers) { restore_interrupt_flags(flags); return 0; }
    bool blocked=wait_on(pipe->read_waiters);
    restore_interrupt_flags(flags);
    if(!blocked) return -1;
  }
}
int32_t pipe_write(PipeState* pipe, const void* buffer, size_t size) {
  size_t total=0;
  const auto* in=static_cast<const uint8_t*>(buffer);
  while(total<size) {
    uint64_t flags=save_interrupt_flags_and_disable();
    if(!pipe->readers) {
      restore_interrupt_flags(flags);
      return total?static_cast<int32_t>(total):kFdBrokenPipe;
    }
    size_t space=kPipeBufferBytes-pipe->count;
    // 小写入是一个不可拆分的记录：不足整块空间时先等，不能写一半后
    // 睡眠，否则另一个写者的记录可能插在中间。大写入允许分块穿插。
    if(space && (size>kPipeBufferBytes || space>=size)) {
      size_t count=size-total<space?size-total:space;
      size_t at=(pipe->head+pipe->count)%kPipeBufferBytes;
      size_t first=count<kPipeBufferBytes-at?count:kPipeBufferBytes-at;
      memory_copy(pipe->bytes+at,in+total,first);
      memory_copy(pipe->bytes,in+total+first,count-first);
      pipe->count+=count; total+=count;
      wake(pipe->read_waiters);
      restore_interrupt_flags(flags);
      continue;
    }
    bool blocked=wait_on(pipe->write_waiters);
    restore_interrupt_flags(flags);
    if(!blocked) return total?static_cast<int32_t>(total):-1;
  }
  return static_cast<int32_t>(total);
}
void release(OpenFileDescription* object) {
  if(!object || --object->references) return;
  if(object->kind==kDescriptorFile) (void)vfs_close_file(&object->file);
  if(object->pipe) {
    auto* pipe=object->pipe;
    uint64_t flags=save_interrupt_flags_and_disable();
    if(object->kind==kDescriptorPipeRead) { --pipe->readers; wake(pipe->write_waiters); }
    else { --pipe->writers; wake(pipe->read_waiters); }
    bool unused=!pipe->readers && !pipe->writers;
    restore_interrupt_flags(flags);
    if(unused) kfree(pipe);
  }
  kfree(object);
}
void attach(FileDescriptorTable* t,int32_t slot,OpenFileDescription* object) {
  t->entries[slot]={object,true}; ++t->open_count;
  if(slot>=static_cast<int32_t>(kFileDescriptorCapacity)) t->standard_closed[slot-kFileDescriptorCapacity]=false;
}
}
int32_t fd_public_to_slot(int32_t fd) {
  if(fd<0 || static_cast<size_t>(fd)>=kPublicFileDescriptorCapacity) return -1;
  return fd<3?static_cast<int32_t>(kFileDescriptorCapacity)+fd:fd-3;
}
int32_t fd_slot_to_public(int32_t fd) {
  return valid(fd)?(fd<static_cast<int32_t>(kFileDescriptorCapacity)?fd+3:fd-kFileDescriptorCapacity):-1;
}
bool initialize_file_descriptor_table(FileDescriptorTable* t,const VfsMount* vfs) {
  if(!t) return false;
  memory_set(t,0,sizeof(*t));
  if(!vfs_is_mounted(vfs)) return false;
  t->vfs=vfs; return true;
}
bool file_descriptor_table_is_ready(const FileDescriptorTable* t) { return structural(t) && vfs_is_mounted(t->vfs); }
FileDescriptorKind fd_kind(const FileDescriptorTable* t,int32_t fd) {
  if(auto* object=description(t,fd)) return object->kind;
  if(!structural(t) || !valid(fd) || fd<static_cast<int32_t>(kFileDescriptorCapacity)) return kDescriptorInvalid;
  int32_t standard=fd-kFileDescriptorCapacity;
  return t->standard_closed[standard]?kDescriptorInvalid:(standard==0?kDescriptorTerminalInput:kDescriptorTerminalOutput);
}
bool fd_is_open(const FileDescriptorTable* t,int32_t fd) { return fd_kind(t,fd)!=kDescriptorInvalid; }
int32_t fd_terminal_number(const FileDescriptorTable* t,int32_t fd) {
  auto* object=description(t,fd);
  return object?object->terminal_number:fd-static_cast<int32_t>(kFileDescriptorCapacity);
}
int32_t fd_open(FileDescriptorTable* t,const char* path,uint32_t flags) {
  if(!file_descriptor_table_is_ready(t) || !path) return -1;
  int32_t slot=empty_slot(t); if(slot<0) return -1;
  size_t length=0; while(length<kFileDescriptorPathCapacity && path[length]) ++length;
  if(!length || length>=kFileDescriptorPathCapacity) return -1;
  // 先分配“账本”，再允许 create/truncate 改磁盘，避免资源不足时意外修改文件。
  auto* object=static_cast<OpenFileDescription*>(kcalloc(1,sizeof(OpenFileDescription)));
  if(!object) return -1;
  VfsMount mount=*t->vfs; VfsStat stat;
  bool exists=vfs_stat(t->vfs,path,&stat);
  if((!exists && (!(flags&kOpenCreate) || !vfs_create_file(&mount,path))) ||
     (exists && stat.type!=kVfsNodeTypeFile) ||
     ((flags&kOpenTruncate) && !vfs_write_file(&mount,path,nullptr,0)) ||
     !vfs_open_file(t->vfs,path,&object->file)) { kfree(object); return -1; }
  object->kind=kDescriptorFile; object->references=1; object->flags=flags;
  memory_copy(object->path,path,length+1); attach(t,slot,object); return slot;
}
bool fd_can_read(const FileDescriptorTable* t,int32_t fd) {
  auto kind=fd_kind(t,fd);
  auto* object=description(t,fd);
  return kind==kDescriptorPipeRead || kind==kDescriptorTerminalInput ||
         (kind==kDescriptorFile && (object->flags&kOpenRead));
}
bool fd_can_write(const FileDescriptorTable* t,int32_t fd) {
  auto kind=fd_kind(t,fd); auto* object=description(t,fd);
  return kind==kDescriptorPipeWrite || kind==kDescriptorTerminalOutput ||
         (kind==kDescriptorFile && (object->flags&kOpenWrite));
}
int32_t fd_read(FileDescriptorTable* t,int32_t fd,void* buffer,size_t bytes) {
  if(!fd_can_read(t,fd) || (!buffer && bytes) || bytes>2147483647U) return -1;
  auto* object=description(t,fd);
  if(!object) return -1;  // 终端由 syscall 层接键盘驱动。
  if(object->kind==kDescriptorPipeRead) return pipe_read(object->pipe,buffer,bytes);
  return object->kind==kDescriptorFile?static_cast<int32_t>(vfs_read_file(&object->file,buffer,bytes)):-1;
}
int32_t fd_write(FileDescriptorTable* t,int32_t fd,const void* buffer,size_t bytes) {
  if(!fd_can_write(t,fd) || (!buffer && bytes) || bytes>2147483647U) return -1;
  if(!bytes) return 0;
  auto* object=description(t,fd); if(!object) return -1;
  if(object->kind==kDescriptorPipeWrite) return pipe_write(object->pipe,buffer,bytes);
  if(object->kind!=kDescriptorFile || !vfs_is_mounted(t->vfs)) return -1;
  Os64FsInode inode; Os64Fs* fs=t->vfs->os64fs;
  if(!os64fs_lookup_path(fs,object->path,&inode) || inode.inode_number!=object->file.handle.inode.inode_number) return -1;
  uint32_t offset=(object->flags&kOpenAppend)?inode.size_bytes:object->file.handle.offset;
  constexpr uint64_t maximum=(kOs64FsDirectBlockCount+128ULL)*512ULL;
  if(bytes>maximum || offset>maximum-bytes) return -1;
  uint64_t end=offset+bytes;
  size_t size=end>inode.size_bytes?end:inode.size_bytes;
  auto* staging=static_cast<uint8_t*>(kcalloc(size,1)); if(!staging) return -1;
  bool ok=!inode.size_bytes || os64fs_read_inode_data(fs,&inode,0,staging,inode.size_bytes);
  if(ok) { memory_copy(staging+offset,buffer,bytes); ok=os64fs_write_file(fs,object->path,staging,size); }
  kfree(staging);
  if(!ok || !os64fs_lookup_path(fs,object->path,&inode)) return -1;
  object->file.handle.inode=inode; object->file.handle.offset=end; return bytes;
}
bool fd_close(FileDescriptorTable* t,int32_t fd) {
  if(!fd_is_open(t,fd)) return false;
  auto* object=description(t,fd);
  t->entries[fd]={nullptr,false};
  if(fd>=static_cast<int32_t>(kFileDescriptorCapacity)) t->standard_closed[fd-kFileDescriptorCapacity]=true;
  if(object) { --t->open_count; release(object); }
  return true;
}
void fd_close_nonstandard(FileDescriptorTable* t) {
  if(t) for(int32_t fd=0;fd<static_cast<int32_t>(kFileDescriptorCapacity);++fd) if(fd_is_open(t,fd)) (void)fd_close(t,fd);
}
void fd_close_all(FileDescriptorTable* t) {
  if(t) for(int32_t fd=0;fd<static_cast<int32_t>(kFileDescriptorSlotCapacity);++fd) if(fd_is_open(t,fd)) (void)fd_close(t,fd);
}
bool fd_inherit(FileDescriptorTable* destination,const FileDescriptorTable* source) {
  if(!structural(destination) || !structural(source) || destination==source) return false;
  fd_close_all(destination);
  destination->vfs=source->vfs;
  for(size_t i=0;i<3;++i) destination->standard_closed[i]=source->standard_closed[i];
  for(size_t i=0;i<kFileDescriptorSlotCapacity;++i) {
    auto* object=description(source,i);
    if(object) { ++object->references; attach(destination,i,object); }
  }
  return true;
}
int32_t fd_dup2(FileDescriptorTable* t,int32_t old_fd,int32_t new_fd) {
  if(!valid(new_fd) || !fd_is_open(t,old_fd)) return -1;
  if(old_fd==new_fd) return new_fd;
  auto* object=description(t,old_fd);
  if(object) ++object->references;
  else {
    object=static_cast<OpenFileDescription*>(kcalloc(1,sizeof(OpenFileDescription)));
    if(!object) return -1;
    object->references=1; object->kind=fd_kind(t,old_fd);
    object->terminal_number=fd_terminal_number(t,old_fd);
  }
  if(fd_is_open(t,new_fd)) (void)fd_close(t,new_fd);
  attach(t,new_fd,object); return new_fd;
}
int32_t fd_dup(FileDescriptorTable* t,int32_t old_fd) {
  if(!structural(t)) return -1;
  int32_t slot=empty_slot(t); return slot<0?-1:fd_dup2(t,old_fd,slot);
}
bool fd_pipe(FileDescriptorTable* t,int32_t slots[2]) {
  if(!structural(t) || !slots) return false;
  int32_t a=empty_slot(t); if(a<0) return false;
  t->entries[a].open=true; int32_t b=empty_slot(t); t->entries[a].open=false;
  if(b<0) return false;
  auto* pipe=static_cast<PipeState*>(kcalloc(1,sizeof(PipeState)));
  auto* read_end=static_cast<OpenFileDescription*>(kcalloc(1,sizeof(OpenFileDescription)));
  auto* write_end=static_cast<OpenFileDescription*>(kcalloc(1,sizeof(OpenFileDescription)));
  if(!pipe || !read_end || !write_end) { kfree(pipe); kfree(read_end); kfree(write_end); return false; }
  pipe->readers=pipe->writers=1;
  read_end->kind=kDescriptorPipeRead; write_end->kind=kDescriptorPipeWrite;
  read_end->references=write_end->references=1; read_end->pipe=write_end->pipe=pipe;
  attach(t,a,read_end); attach(t,b,write_end); slots[0]=a; slots[1]=b; return true;
}
bool fd_references_inode(const FileDescriptorTable* t,uint32_t inode) {
  if(!structural(t)) return false;
  for(size_t i=0;i<kFileDescriptorSlotCapacity;++i) {
    auto* object=description(t,i);
    if(object && object->kind==kDescriptorFile && object->file.handle.inode.inode_number==inode) return true;
  }
  return false;
}
bool fd_stat(const FileDescriptorTable* t,int32_t fd,VfsStat* stat) {
  auto* object=description(t,fd); return object && object->kind==kDescriptorFile && stat && vfs_file_stat(&object->file,stat);
}
bool fd_seek(FileDescriptorTable* t,int32_t fd,uint32_t offset) {
  auto* object=description(t,fd); return object && object->kind==kDescriptorFile && vfs_seek_file(&object->file,offset);
}
uint32_t fd_tell(const FileDescriptorTable* t,int32_t fd) {
  auto* object=description(t,fd); return object && object->kind==kDescriptorFile?vfs_tell_file(&object->file):0;
}
uint32_t fd_open_count(const FileDescriptorTable* t) { return structural(t)?t->open_count:0; }
