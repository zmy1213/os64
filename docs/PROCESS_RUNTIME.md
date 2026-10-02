# 用户进程运行时

os64 的当前进程运行时面向 BIOS 启动、单 CPU 的 x86_64 教学系统。它能从
OS64FS 装载自己的 ELF64 程序，在 ring 3 运行，通过 `int 0x80` 调用内核，
等待退出并回收资源。下面描述当前代码的行为和限制；较早教程中的一次性
用户态烟测仍保留，用于检查最底层切换链路。

## 从 shell 到用户程序

例如：

```text
run /bin/hello Alice
run /bin/echo hello os64
run /bin/spawn_test
ps
mem
```

`scheduler_create_user_elf_thread` 依次创建 PCB、克隆内核页表、初始化进程
自己的 cwd/fd 表和输出回调、装载 ELF、映射 64 KiB 用户栈并留下保护页、初始化用户堆边界，再创建用户 TCB。
`scheduler_prepare_user_arguments` 把参数字符串及指针写入这份新地址空间。
shell 等待该 PID 退出，打印退出码，然后显式回收它，因此反复运行程序
不会把固定的 PCB/TCB 槽位永久占满。

用户入口 `user/start.asm` 读取初始 RSP 上的下列布局，再调用
`main(argc, argv)`：

```text
低地址 / 初始 RSP
  argc                         uint64_t
  argv[0]                      用户虚拟地址
  argv[1] ... argv[argc - 1]    用户虚拟地址
  NULL                         uint64_t
  对齐空隙和以 NUL 结尾的参数字符串
高地址 / 用户栈顶 0x800000
```

公开的 spawn 接口当前最多接受 8 个参数，每个字符串最多 63 字节。
用户栈为 64 KiB（16 页），参数和调用栈共同使用它；下方的 4 KiB 页保持未映射。
这不是自动增长栈，过深递归仍会触发页错误。没有参数时，spawn 自动把程序路径作为 argv[0]。

## 创建、等待和退出

`user/os64.hpp` 提供 `spawn(path, argv, argc)`、`waitpid(pid, &status)`、
`getpid()`、`sleep(milliseconds)`、`yield()` 和 `exit(status)` 的包装。

| 系统调用编号 | 行为 |
| --- | --- |
| 10 exit | 结束当前用户线程，最后一条线程退出时进程进入 exited |
| 11 yield | 主动让出 CPU，恢复后从原 syscall 的下一条指令继续 |
| 13 getpid | 返回当前进程 PID |
| 14 sleep | 毫秒换算为 PIT tick 后阻塞，到期由 timer 唤醒 |
| 15 spawn | 装载新 ELF，继承 cwd 和打开的描述符，成功返回子 PID |
| 16 waitpid | 只允许等待自己的子进程，获取退出码并回收其资源 |
| 20 brk | 查询/改变当前进程用户堆末尾，立即分配或释放页 |
| 21 replace_file | 在一次文件事务中创建或替换整个文件，供编辑器保存 |

spawn 设置 `parent_pid` 为当前线程所属进程的 PID。新进程获得独立 fd 表，
表内引用继承父进程打开的文件或管道，普通文件的偏移也由这些引用共享。
子进程关闭自己的 fd 不会删除父进程的引用；管道最后一个写端引用关闭后，
读者读完缓存才看到 EOF。cwd 及输出回调由系统调用层复制。

内核的 `scheduler_wait_process` 只等待，不回收。调用者阻塞在
`waiting_for_pid` 上；目标进程最后一条线程退出时唤醒等待者。这比循环
yield 更可靠：高优先级父线程不会为了等待而一直占据最高 ready 队列。
`scheduler_reap_process` 负责后续回收，`scheduler_discard_process` 负责
尚未运行的进程在参数准备失败时撤销创建。

PCB 内部保存 64 位退出码，保留早期烟测编码的完整值；公开 waitpid 的
status 指向 32 位整数。普通程序应使用能放进 int32_t 的退出码。

父进程先退出时，其子进程被标为 `auto_reap` 并解除原父子关系。子进程
可以继续运行；退出后，调度器在另一个线程的栈上、idle 循环或 bootstrap
恢复点回收它。这样既不留下永久孤儿 zombie，也不会释放正在执行的栈。
较早烟测从 bootstrap 创建的无父进程对象没有这个自动回收标志，以便测试
读取 finished TCB 的 trap frame 和计数。

## 每个进程拥有哪些资源

每次正式启动成功后，进程拥有：

- 从内核 root 克隆的私有页表树；spawn 不从父用户 root 复制用户页。
- ELF PT_LOAD 段的用户物理页、16 页用户栈，以及运行时申请的用户堆页。
- 两根各 32 KiB 的 supervisor 内核栈，分配自可回收内核堆。
- 自己的 fd 表、cwd、退出状态，以及 PCB/TCB 槽位。

两根内核栈有不同职责：bootstrap 栈保存 `user_mode_enter` 最终返回所需的
内核调用现场；TSS.rsp0 指向另一根进入栈，接收 ring 3 触发的 syscall、
异常和硬件中断。它们分开后，连续 int80 不会覆盖最终退出恢复点。

32 KiB 进入栈还为文件系统调用保留足够空间。分配位图校验函数自身有一块
4096 字节的局部数组，原来的一页内核进入栈不足以容纳这一函数和调用链。

回收包括关闭 fd、释放两根堆栈、用户数据叶页和私有页表，并清除 PCB/TCB。
内核 supervisor 叶页由所有进程共享，不随某个进程退出而释放。
ELF staging 页在所有返回路径上释放；装载或线程创建失败时撤销整个新进程。
页表克隆失败时也会递归回收已经克隆的部分。

## 地址空间与分配限制

| 地址范围 | 当前用途 |
| --- | --- |
| 0–2 MiB 虚拟地址 | 保留启动恒等映射，低地址内核及早期数据 |
| 物理 `0x100000`–`0x160000` | 独立内核 BSS 保留窗口；entry64 显式清零 |
| 物理 `0x170000`–`0x180000` | 专门保留 64 KiB bootstrap 内核栈，栈顶由 entry64 设置 |
| `0x400000`–`0x800000` 虚拟地址 | 每个进程独立的 4 MiB 用户窗口 |
| ELF 段结束后的页边界–当前 break | 已申请的用户堆，最大末尾 `0x7ef000` |
| `0x7ef000`–`0x7f0000` | 不映射的 4 KiB 栈 guard |
| `0x7f0000`–`0x800000` | 64 KiB 用户栈，向低地址使用 |
| 16–20 MiB 虚拟地址 | 固定 4 MiB 内核堆，supervisor 映射 |
| `0xffff800000000000` 起 | 内核对物理 0–256 MiB 的 supervisor 直接映射 |

表中每个上界都不包含自身。`entry64.asm` 把内核入口 RSP 设置为物理
`0x180000` 对应的低恒等映射地址；这根 bootstrap 栈不在低 1 MiB 内。
物理分配器必须额外保留 `0x170000`–`0x180000`，否则更大的用户堆可能覆盖
仍在使用的启动调用现场。低 1 MiB 则整体保留给 bootloader、内核文件、启动卷及设备区间；BSS 窗口还要另外保留，避免全局状态被发给用户页。

创建正式用户进程之前，内核预映射完整 4 MiB 内核堆，再克隆页表树。
后续 syscall 中申请的 fd 缓冲、事务对象和内核栈因此能在所有进程中使用
相同内核虚拟地址。内核 supervisor 物理页共享；私有页表树分别拥有并回收。
堆内对象通过 kfree 复用容量，固定堆数据页不在每次 kfree 时返还物理分配器。

### 物理页与直接映射

`initialize_page_allocator` 通过 E820 构造 available/allocated 双位图，总计
16 KiB。一页对应一位，管理上限为 256 MiB；实际可用容量依机器 E820 决定，
默认运行脚本给 QEMU 128 MiB。只接受完整的 usable 页，保留/禁用区间优先排除。
`free_page` 检查可分配属性和 ownership，拒绝非本分配器页面与重复释放。

`paging_initialize_direct_map` 先借旧低 2 MiB 映射建立两张 bootstrap 页表，
用 supervisor-only 的 2 MiB 大页建立高地址访问窗口。之后所有管理范围内的
页表页与用户物理页都能通过 `paging_physical_pointer` 访问：

```text
内核访问指针 = 0xffff800000000000 + 物理地址
```

CR3 和 PTE 仍保存物理地址，不把直接映射指针写进页表的物理地址字段。
映射整个范围只意味着内核能寻址；它不会把设备洞或 BIOS reserved 变成可分配 RAM。
相比历史的 1–2 MiB 专用低页池，页表与用户数据页现在可以从整个管理范围分配和回收。

### ELF 与页面保护

ELF loader 接受 little-endian x86_64 ET_EXEC，最多 8 个 program headers、
合计 256 张 PT_LOAD 页，文件自身最多 69,632 字节。文件由 kmalloc 暂存，
所有返回路径释放缓冲。装载前检查长度/地址溢出、段页面重叠、入口属于可执行段，
拒绝 RWX 段，拒绝任何段侵入 guard/栈；各页先清零，再逐页拷贝文件内容。
BSS 的运行内存长度可以大于文件长度，仍受 256 页限制。

CPUID 支持 NX 时内核设置 EFER.NXE，代码页不可写，数据、用户堆和栈 NX；
高直接映射及内核堆也不可执行。CPU 不支持 NX 时不设置非法 bit 63，启动日志
明确报告 `nx_enabled=0`，此时不能宣称硬件禁止执行数据页。没有 ASLR。

### 用户堆 brk

每进程保存 `user_heap_base`、`user_heap_break`、`user_heap_limit`。
base 是 ELF 所有段结束后向上页对齐的地址，初始 break 等于 base，limit
为栈 guard 起点 `0x7ef000`。`sys_brk(0)` 查询；`sys_brk(new_break)` 成功
返回请求地址，越界或缺内存时返回旧 break，所以调用方必须比较实际返回值。

break 按字节记录，物理页按 4 KiB 分配。扩张立即分配并清零新页；中途失败
撤销本次映射。缩小释放不再需要的整页；保留页中被裁掉的尾部清零，防止
重新增长时暴露旧数据。`address_space_unmap_user_page` 同时回收空的私有页表层级，
修改当前 CR3 的映射后用 invlpg 使对应缓存失效。没有 mmap、共享内存、COW、
demand paging 或交换区。

用户库 [memory.hpp](../user/memory.hpp) 的 `memory::allocate/release/resize`
在 brk 之上提供小块分配、16 字节对齐、分割合并和尾部归还。应用不能在库
仍拥有活块时绕过它手动移动 break；这是单线程教学库，不是完整 libc。

## 切换和中断

上下文切换同时保存 RSP、callee-saved 寄存器、CR3、RFLAGS 和每线程
16 字节对齐的 512 字节 x87/SSE FXSAVE 现场；bootstrap 也有独立副本。切栈期间
关闭中断，恢复目标上下文后恢复其 IF 状态。否则，用户 int80 进入内核时
清掉的 IF 可能被 shell 继承，导致等待键盘时再也收不到中断。

PIT IRQ0 完成 tick 记账后、在可能切换线程之前发送 PIC EOI。中断处理函数
可能被挂起很久；先 EOI 才能让其他线程继续接收 timer tick，保证 sleep
唤醒和用户态抢占正常工作。CPU 在用户态运行时可以被 timer 抢占；内核态
工作仍依赖明确的 yield、sleep、block 等调度点。

## 用户错误的边界

所有用户指针在 syscall 分发前检查：范围必须完整落在用户窗口，每一页的
四级页表都必须 present、user，输出缓冲还必须 writable。路径字符串需要
在长度限制内终止。这可以拒绝未映射指针、内核地址及只读 text 输出指针。

来自 ring 3 的 page fault、invalid opcode、general protection fault 等
会记录异常向量及地址，结束当前进程并返回调度器。退出码约定为
`128 + exception_vector`，例如 page fault 为 142。shell 可以继续运行其他
程序。内核自身的异常仍走诊断及停机路径；用户错误隔离不能修复内核错误。

## 当前边界与验证

PCB 固定 16 个，TCB 固定 32 个，idle 和 shell 也占用槽位。实现面向单 CPU、
单个用户主线程的自有程序，没有 fork、exec 替换、signals、用户 mmap、
动态链接器、POSIX/Linux ABI、AVX/XSAVE、SMP、权限用户模型或 demand paging。
已实现的 x87/SSE 现场由每个 TCB 的 16 字节对齐 512 字节缓冲保存，切换汇编
在关中断后执行 FXSAVE64/FXRSTOR64，避免 IRQ 观察到只切了一半的线程状态。
进程退出立即关闭文件、管道及归属的 UDP 句柄；页面和内核栈在 wait/reap 时释放。
父子描述符及 dup 共享打开对象和文件偏移。详细操作见 [IPC/Shell 教程](./IPC_SHELL_TUTORIAL.md)。
用户栈已有 guard 与固定 64 KiB 映射；内核栈仍没有普遍的硬件溢出 guard。

`make test-system` 覆盖正式启动、argv、spawn/wait、用户 sleep/抢占、坏指针、
用户 fault 后继续运行，以及连续 55 次程序启动后空闲物理页数保持不变。
较早 `make test-stage1` 保留寄存器、trap frame、TSS、调度与 shell 烟测。
`make test-memory-host` 覆盖 E820 重叠/禁用区、bootstrap 栈保留、分配与重复释放。
`make test-memory-user` 在 QEMU 中覆盖大于旧 4 KiB 上限的 ELF、1 MiB 堆、
缩堆清零/边界拒绝、16 KiB 实际局部栈数组、重复运行回收、stack guard/NX、
编辑器多行/超限处理与冷启动保存；NX 测试以启动日志支持状态为条件。
完整测试命令和最新结果以仓库 README 及实际测试输出为准。

`make test-performance` 在 QEMU 中检查 x87/SSE 初始化与隔离、8/12 工作者
结果/页回收、八工作者定时器进度、32 MiB 管道和性能/日志 ABI 的指针边界。
`make test-log-host` 用 sanitizer 检查日志环与独立计算验证器。测时、p50/p99、
单核限制及可选 Linux 客体对照见 [性能教程](./PERFORMANCE_TUTORIAL.md)。
