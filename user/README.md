# 用户程序、内存库与行式编辑器

`programs/*.cpp` 是独立的 x86_64 ELF 用户程序，在各自的地址空间中以 CPL 3（ring 3）运行。它们通过 `int 0x80` 请求内核服务，不链接宿主 C 库。第一次使用先读 [当前版本入门教程](../docs/BEGINNER_TUTORIAL.md)，生命周期和内存细节见 [PROCESS_RUNTIME.md](../docs/PROCESS_RUNTIME.md)。

## 构建和安装到客体

在宿主仓库根目录执行 `make users` 只编译用户 ELF；`make build` 同时生成内核、启动镜像及含 `/bin` 的数据盘模板。首次构建创建 `build/data.img`，已有数据盘会保留，因此其中的旧工具不会因重新构建自动更新。

更新已有盘中的 `/bin` 时，先在 os64 中 `shutdown` 并确认 QEMU 退出，再在宿主执行：

```bash
make update-tools
```

更新在副本上通过真实 OS64FS 完成，只改变 `/bin`；验证和重新挂载成功后替换镜像，保留用户文件并生成完整备份。盘在用、空间不足等失败不会替换原盘。此目标更新默认 `build/data.img`。`make reset-data` 会删除所有已保存客体文件，不是安装新版工具的推荐步骤。

## 当前工具

| 类别 | 程序 | 示例（在 os64 Shell 输入） |
| --- | --- | --- |
| 参数/输出 | hello、echo | `run /bin/hello Alice` |
| 文件观察 | pwd、ls、cat | `run /bin/ls /work` |
| 文件修改 | writer、mkdir、rm、sync | `run /bin/writer /work/note.txt hello` |
| 进程/等待 | spawn_test、sleep、spin | `run /bin/spawn_test` |
| 用户内存 | mem_test | `run /bin/mem_test` |
| 行式编辑 | edit | `run /bin/edit /work/lesson.txt` |
| 故障演示 | badptr、fault、ud2、stackfault、nxfault | `run /bin/stackfault` |
| 文件块测试 | fs_test | `run /bin/fs_test /work/large.txt` |

`writer` 会替换指定文件；`fs_test` 普通模式也会写指定文件，`verify` 参数只核对内容。实验时选尚未保存个人内容的路径。

`fault` / `ud2` 故意触发异常。`stackfault` 写不映射的 `0x7ef000` 栈 guard；`nxfault` 尝试执行 RW 数据页（仅 CPU 支持且启用 NX 时要求失败）。它们退出后 Shell 应继续工作；页错误为 142，非法指令为 134。`badptr` 则验证系统调用拒绝非法指针而不终止程序。

`mem_test` 使用真实的 8 KiB 只读查找表，使 ELF 超过旧 4 KiB 限制；验证实际 16 KiB 栈数组、brk 清零/缩小/边界拒绝、超过 1 MiB 堆、空块复用与尾部回收。普通运行不写磁盘，关键成功输出是：

```text
mem_test large_elf_ok
mem_test large_stack_ok
mem_test brk_zero_reject_ok
mem_test heap_1m_reuse_ok
run_exit_code=0
```

## 编辑器：完整可操作示例

先在 os64 Shell 建好目录并打开一个实验文件：

```text
mkdir /work
run /bin/edit /work/lesson.txt
```

`/work` 已存在时沿用即可。新文件会显示 `edit: new file; use save to create it`；存在的文件会显示已载入字节数。编辑器提示符是 `edit> `，与 Shell 的 `os64 % ` 不同。

如果这个文件原本为空，在编辑器逐行输入：

```text
append first line
append third line
insert 2 second line
print
delete 1
print
save
quit
```

第一次 print 应显示三行：

```text
1: first line
2: second line
3: third line
```

删除第 1 行后，应显示 `1: second line`、`2: third line`。保存应显示 `edit: saved 23 bytes`；退出后 Shell 打印 `run_exit_code=0`。在 Shell 输入 `cat /work/lesson.txt`，再 `reboot` 后重新 `cat`，内容应是：

```text
second line
third line
```

| 编辑器命令 | 行为 |
| --- | --- |
| `print` | 显示行号、文本和总字节数 |
| `append TEXT` | 在末尾追加一行；`append` 可追加空行 |
| `insert N TEXT` | 在第 N 行之前插入，N=当前行数+1 时追加 |
| `delete N` | 删除第 N 行，行号从 1 开始 |
| `save` | 将整个缓冲作为一次文件事务保存，再同步 |
| `quit` | 无未保存改动时退出；否则提醒先保存 |
| `quit!` | 明确丢弃内存中的未保存改动并退出 |
| `help` | 显示命令帮助 |

append/insert 会补换行；载入已有无末尾换行的文件时先保留原样，之后末尾追加会补分隔换行。TEXT 是命令后剩余的文字，不做 Shell 式引号剥离，例如输入 `append "hello"` 会保存两个引号。编辑器没有单独 replace-line 命令，可先 delete 再 insert。

当前只编辑不含 NUL 字节的文本，最大 32,768 字节；命令行最多 512 字节。超限、读失败和二进制文件会明确拒绝，原文件不被截短或修改。超长输入会一直读到回车再拒绝整条命令，避免剩下半条内容被当下一条指令。回显和退格由用户程序处理；没有全屏光标编辑、Unicode 输入法或语法高亮。

save 使用 syscall 21 `replace_file`，避免先 `open(TRUNC)`、随后 write 失败而留下空原文件。失败时缓冲留在内存中，尚未确认保存；若设备持续失败，内核文件系统可能取消挂载。它提供运行时失败处理，没有断电日志，不能保证提交中途强制终止仍可恢复。

相关源码：[edit.cpp](programs/edit.cpp)、[memory.hpp](memory.hpp)、[memory.cpp](memory.cpp)、[os64.hpp](os64.hpp)。

## 用户堆库

`memory::allocate(size)`、`memory::release(pointer)`、`memory::resize(pointer, size)` 是这个项目自己的小块分配器，不是完整 libc 的 malloc/free。返回地址按 16 字节对齐。

每块前有 32 字节 Block 账本，记录 payload 容量、前后链接和是否空闲。allocate 先找能复用的空块；余量能放下账本和至少 16 字节时分割。没有空块才通过 brk 扩张。release 标为空闲，合并相邻空块；堆尾整块空闲时将 break 缩到该账本起点，把页归还内核。中间的洞只能复用，不能把仍有活对象的后半段一起释放。

使用约定：

- allocate(0) 与无法分配时返回 nullptr；release(nullptr) 无操作。
- release 只接受本库返回、尚未释放的指针；不接受栈指针、内部偏移或重复释放。
- resize(nullptr, n) 等同 allocate(n)；resize(p, 0) 释放并返回 nullptr。
- resize 扩大保留旧内容；失败原指针仍有效。当前缩小保留原容量。
- 不支持多线程并发；库仍有活对象时不要手动调用 brk 移动堆边界。

例子在用户源码中编写，不是 Shell 命令：

```cpp
#include "os64.hpp"
#include "memory.hpp"
extern "C" int main(int, char**) {
  auto* text = static_cast<char*>(memory::allocate(8192));
  if (!text) return 1;
  text[0] = 'A';
  write(1, text, 1);
  memory::release(text);
  return 0;
}
```

库还提供 memcpy/memmove/memset/memcmp，既供源码使用，也满足编译器生成的内存操作。memmove 支持源目标重叠；memcpy 不承担这个约定。

## 入口栈、ELF 与 ABI

`start.asm` 读初始栈上的 argc、argv 指针表和末尾 NULL，再调用 `main(argc, argv)`，返回值交给 syscall 10 exit。初始 RSP 按 16 字节对齐。最多 8 个参数（含程序名），每个最多 63 字节；没有传 argv 时 spawn 自动把路径当 argv[0]。

用户窗口为 4–8 MiB，ELF 加载段最多 256 页，文件最多 69,632 字节。用户栈固定 64 KiB，下方保留一页 guard；堆在 ELF 段后通过 brk 立即分配，上限 `0x7ef000`。NX 支持时 RW 数据、堆与栈不可执行，loader 拒绝 RWX。完整布局见 [进程运行时](../docs/PROCESS_RUNTIME.md)。

系统调用使用 RAX=编号，RDI/RSI/RDX/RCX=参数，RAX=有符号结果。编号如下，具体结构/包装见 `os64.hpp` 与内核 `syscall.hpp`：

| 编号 | 服务 |
| --- | --- |
| 0–8 | getcwd、chdir、旧只读 open、read、close、seek、stat、stat_path、listdir |
| 9–11 | write、exit、yield |
| 12–14 | mkdir、getpid、sleep |
| 15–18 | spawn、waitpid、unlink、sync |
| 19 | open-with-flags，READ=1 / WRITE=2 / CREATE=4 / TRUNC=8 / APPEND=16 |
| 20 | brk(0) 查询；brk(address) 返回实际末尾，失败返回旧末尾 |
| 21 | replace_file(path, buffer, size)，单次事务创建/覆盖，返回字节数或负错误 |

fd 0/1/2 是标准输入/输出/错误，普通文件从 3 开始。spawn 继承 cwd 和控制台输出，不复制父进程的普通 fd。用户态 syscall 指针会检查页映射、user 和写权限；它不是 POSIX/Linux ABI。

构建的 `user.ld` 将 text/rodata 放 RX 段、data/BSS 放 RW 段。C++ 声明“没有 const”不保证优化后的对象一定落 RW：如果从未修改，编译器可能优化为只读常量。验证 NX 测试时要看最终 ELF program headers 和地址归属；`test-memory-user.py` 会检查实际布局，而不是只相信源码名称。FPU/SIMD 上下文尚未保存，内核与用户仍以 `-mgeneral-regs-only` 编译。
