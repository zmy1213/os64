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
自己的 cwd/fd 表和输出回调、装载 ELF、映射一页用户栈，再创建用户 TCB。
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
用户栈仍只有 4096 字节，参数和程序自己的调用栈共同使用这一页；大数组或
深递归应避免放在用户栈上。没有参数时，spawn 自动把程序路径作为 argv[0]。

## 创建、等待和退出

`user/os64.hpp` 提供 `spawn(path, argv, argc)`、`waitpid(pid, &status)`、
`getpid()`、`sleep(milliseconds)`、`yield()` 和 `exit(status)` 的包装。

| 系统调用编号 | 行为 |
| --- | --- |
| 10 exit | 结束当前用户线程，最后一条线程退出时进程进入 exited |
| 11 yield | 主动让出 CPU，恢复后从原 syscall 的下一条指令继续 |
| 13 getpid | 返回当前进程 PID |
| 14 sleep | 毫秒换算为 PIT tick 后阻塞，到期由 timer 唤醒 |
| 15 spawn | 装载新 ELF，继承 cwd 和控制台输出，成功返回子 PID |
| 16 waitpid | 只允许等待自己的子进程，获取退出码并回收其资源 |

spawn 设置 `parent_pid` 为当前线程所属进程的 PID。新进程获得独立 fd 表，
不会复制父进程已经打开的普通文件描述符。cwd 及输出回调由系统调用层复制。

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
- ELF PT_LOAD 段的用户物理页，以及一页初始用户栈。
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

| 虚拟地址范围 | 当前用途 |
| --- | --- |
| 0–2 MiB | 启动恒等映射，内核、早期页表及低物理页访问 |
| 4–8 MiB | 每个进程独立的用户窗口，栈从 8 MiB 向下增长 |
| 16–20 MiB | 内核堆，supervisor 映射 |

创建调度用户进程之前，内核预映射完整的 4 MiB 堆容量，再克隆用户页表。
因此，后续用户 syscall 中申请的 fd 缓冲、文件系统事务对象和内核栈，
在用户 root 和内核 root 下都能通过相同虚拟地址访问。内核堆数据页从
2 MiB 以上分配，保留有限的低页供页表和用户程序使用。

当前页表及用户物理页仍要求落在 1–2 MiB 可分配池中。`alloc_page_below`
限制分配范围，`free_page` 使用 ownership 位图拒绝重复释放，并通过可回收
页链复用该池。高地址内核堆物理页不逐页返还给物理分配器；堆内对象通过
kfree 复用已经映射的容量。这还不是通用物理内存管理器。

ELF loader 当前接受 little-endian x86_64 ET_EXEC、最多 8 个 program
headers、最多 32 张 PT_LOAD 页，文件自身不超过 4096 字节。构建使用紧凑
文件偏移和分离的虚拟段地址。加载前检查范围和重叠，入口必须位于可执行
PT_LOAD 段；段的写权限进入 PTE。尚未实现 NX，因此数据页仍可能执行。

## 切换和中断

上下文切换同时保存 RSP、callee-saved 寄存器、CR3 和 RFLAGS。切栈期间
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

PCB 固定 8 个，TCB 固定 16 个，idle 和 shell 也占用槽位。实现面向单 CPU、
单个用户主线程的自有程序，没有 fork、exec 替换、signals、用户 mmap/brk、
动态链接器、POSIX/Linux ABI、FPU/SIMD 上下文、SMP、权限用户模型、用户栈
guard page 或 demand paging。内核栈虽然加大，仍没有硬件溢出 guard。

`make test-system` 覆盖正式启动、argv、spawn/wait、用户 sleep/抢占、坏指针、
用户 fault 后继续运行，以及连续 55 次程序启动后空闲物理页数保持不变。
较早 `make test-stage1` 保留寄存器、trap frame、TSS、调度与 shell 烟测。
完整测试命令和最新结果以仓库 README 及实际测试输出为准。
