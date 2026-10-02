#include "os64.hpp"
#include "memory.hpp"

namespace {
// 32 KiB 是这节教程的编辑器界限，不是文件系统的最大文件大小。
// 超出界限会拒绝操作，绝不会把被截短的内容当成完整文件保存。
constexpr size_t kDocumentLimit=32*1024;
constexpr size_t kCommandLimit=512;
struct Buffer {
  char* data=nullptr;
  size_t size=0;
  size_t capacity=0;
  bool reserve(size_t wanted) {
    if(wanted<=capacity) return true;
    size_t grown=capacity?capacity:128;
    while(grown<wanted) grown*=2;
    char* replacement=static_cast<char*>(memory::resize(data,grown));
    if(!replacement) return false;
    data=replacement;
    capacity=grown;
    return true;
  }
  void clear() { size=0; if(data) data[0]='\0'; }
  void destroy() { memory::release(data); data=nullptr; size=capacity=0; }
};

bool equal(const char* a, const char* b) {
  while(*a && *a==*b) { ++a; ++b; }
  return *a==*b;
}
bool begins(const char* text, const char* prefix) {
  while(*prefix) if(*text++!=*prefix++) return false;
  return true;
}

// 内核的 read(0) 返回原始字符。回显、退格和组装一行由这个用户程序完成。
bool command_line(Buffer& command) {
  command.clear();
  bool overflow=false;
  for(;;) {
    char character=0;
    if(read(0,&character,1)!=1) return false;
    if(character=='\r' || character=='\n') {
      print("\n");
      if(overflow) {
        error("edit: command too long or out of memory; nothing changed\n");
        command.clear();
      }
      return true;
    }
    if(character=='\b' || character==127) {
      if(!overflow && command.size) {
        command.data[--command.size]='\0';
        print("\b \b");
      }
      continue;
    }
    if(static_cast<unsigned char>(character)<32) continue;
    if(overflow) continue;  // 读到回车再返回，不能把半条命令留给下一轮。
    if(command.size>=kCommandLimit || !command.reserve(command.size+2)) {
      overflow=true;
      continue;
    }
    command.data[command.size++]=character;
    command.data[command.size]='\0';
    write(1,&character,1);
  }
}

bool load(Buffer& document, const char* path) {
  int fd=open(path,READ);
  if(fd==-2) {
    print("edit: new file; use save to create it\n");
    if(!document.reserve(1)) return false;
    document.data[0]='\0';
    return true;
  }
  if(fd<0) { error("edit: cannot open file\n"); return false; }
  // 分块读到 EOF，而不是只读一个固定小数组：大文件不能悄悄丢后半段。
  char chunk[512];
  bool ok=true;
  for(;;) {
    int64_t count=read(fd,chunk,sizeof(chunk));
    if(count<0) { error("edit: read failed; original file unchanged\n"); ok=false; break; }
    if(count==0) break;
    if(static_cast<size_t>(count)>kDocumentLimit-document.size) {
      error("edit: file exceeds 32768 bytes; original file unchanged\n");
      ok=false; break;
    }
    if(!document.reserve(document.size+static_cast<size_t>(count)+1)) {
      error("edit: out of memory; original file unchanged\n"); ok=false; break;
    }
    memcpy(document.data+document.size,chunk,static_cast<size_t>(count));
    document.size+=static_cast<size_t>(count);
  }
  if(close(fd)!=0) ok=false;
  if(!ok || !document.reserve(document.size+1)) return false;
  document.data[document.size]='\0';
  // 零字节通常意味着这是二进制文件。本教程只编辑文本，保留它原样。
  for(size_t i=0;i<document.size;++i) if(document.data[i]=='\0') {
    error("edit: binary file is unsupported; original file unchanged\n");
    return false;
  }
  print("edit: loaded "); number(document.size); print(" bytes\n");
  return true;
}

size_t line_count(const Buffer& document) {
  size_t lines=0;
  for(size_t i=0;i<document.size;++i) if(document.data[i]=='\n') ++lines;
  if(document.size && document.data[document.size-1]!='\n') ++lines;
  return lines;
}
size_t line_start(const Buffer& document, size_t line) {
  if(line==1) return 0;
  for(size_t i=0;i<document.size;++i)
    if(document.data[i]=='\n' && --line==1) return i+1;
  return document.size;
}

void show(const Buffer& document) {
  print("edit: "); number(line_count(document)); print(" lines, ");
  number(document.size); print(" bytes\n");
  size_t at=0;
  size_t line=1;
  while(at<document.size) {
    size_t end=at;
    while(end<document.size && document.data[end]!='\n') ++end;
    number(line++); print(": "); write(1,document.data+at,end-at); print("\n");
    at=end<document.size?end+1:end;
  }
}

bool insert_line(Buffer& document, size_t line, const char* text) {
  size_t lines=line_count(document);
  if(line==0 || line>lines+1) { error("edit: line number out of range\n"); return false; }
  size_t count=length(text);
  bool separator=line==lines+1 && document.size && document.data[document.size-1]!='\n';
  size_t added=count+1+(separator?1:0);
  if(added>kDocumentLimit-document.size) {
    error("edit: document limit is 32768 bytes; nothing changed\n"); return false;
  }
  if(!document.reserve(document.size+added+1)) {
    error("edit: out of memory; nothing changed\n"); return false;
  }
  size_t at=line_start(document,line);
  memmove(document.data+at+added,document.data+at,document.size-at);
  if(separator) document.data[at++]='\n';
  memcpy(document.data+at,text,count);
  document.data[at+count]='\n';
  document.size+=added;
  document.data[document.size]='\0';
  return true;
}

bool delete_line(Buffer& document, size_t line) {
  if(line==0 || line>line_count(document)) {
    error("edit: line number out of range\n"); return false;
  }
  size_t at=line_start(document,line);
  size_t end=at;
  while(end<document.size && document.data[end]!='\n') ++end;
  if(end<document.size) ++end;
  memmove(document.data+at,document.data+end,document.size-end);
  document.size-=end-at;
  document.data[document.size]='\0';
  return true;
}

bool line_argument(const char*& text, size_t* value) {
  size_t n=0;
  if(*text<'0' || *text>'9') return false;
  while(*text>='0' && *text<='9') {
    unsigned digit=static_cast<unsigned>(*text++-'0');
    if(n>(SIZE_MAX-digit)/10) return false;
    n=n*10+digit;
  }
  if(*text && *text!=' ') return false;
  *value=n;
  if(*text==' ') ++text;
  return true;
}

void help() {
  print("edit commands (line numbers start at 1):\n"
        "  print             show numbered lines\n"
        "  append TEXT       add a line at the end\n"
        "  insert N TEXT     insert before line N; N=last+1 appends\n"
        "  delete N          remove line N\n"
        "  save              write the whole file and flush the disk\n"
        "  quit              leave after saving\n"
        "  quit!             discard unsaved changes and leave\n"
        "  help              show these commands\n"
        "Text limit: 32768 bytes; command limit: 512 bytes.\n"
        "Changes stay in memory until save.\n");
}

bool save(const Buffer& document, const char* path) {
  // syscall 21 使用文件系统已有的单次完整替换事务。
  // 不能先 open(TRUNC) 再 write：第二步失败时，旧文件已经被第一步清空。
  int64_t result=replace_file(path,document.data,document.size);
  if(result!=static_cast<int64_t>(document.size)) {
    error("edit: save failed; edits remain in memory\n"); return false;
  }
  if(sync()!=0) {
    error("edit: disk flush failed; save is not confirmed\n"); return false;
  }
  print("edit: saved "); number(document.size); print(" bytes\n");
  return true;
}
}

extern "C" int main(int argc, char** argv) {
  if(argc!=2) { error("usage: run /bin/edit PATH\n"); return 1; }
  Buffer document;
  Buffer command;
  if(!load(document,argv[1])) { document.destroy(); return 2; }
  help();
  bool dirty=false;
  int result=0;
  for(;;) {
    print("edit> ");
    if(!command_line(command)) { error("edit: input failed\n"); result=3; break; }
    const char* text=command.data?command.data:"";
    if(!*text) continue;
    if(equal(text,"print")) show(document);
    else if(equal(text,"help")) help();
    else if(equal(text,"save")) { if(save(document,argv[1])) dirty=false; }
    else if(equal(text,"quit!") || equal(text,"quit")) {
      if(dirty && equal(text,"quit")) {
        error("edit: unsaved changes; use save, or quit! to discard\n"); continue;
      }
      break;
    } else if(equal(text,"append") || begins(text,"append ")) {
      const char* contents=text+6;
      if(*contents==' ') ++contents;
      if(insert_line(document,line_count(document)+1,contents)) dirty=true;
    } else if(begins(text,"insert ")) {
      text+=7;
      size_t line=0;
      if(!line_argument(text,&line)) error("usage: insert N TEXT\n");
      else if(insert_line(document,line,text)) dirty=true;
    } else if(begins(text,"delete ")) {
      text+=7;
      size_t line=0;
      if(!line_argument(text,&line) || *text) error("usage: delete N\n");
      else if(delete_line(document,line)) dirty=true;
    } else error("edit: unknown command; type help\n");
  }
  command.destroy();
  document.destroy();
  return result;
}
