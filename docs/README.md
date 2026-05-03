# 文档阅读顺序

这几份文档不是并列关系，而是有明显先后顺序的。

如果你现在是从零学这个项目，最合理的阅读顺序是：

1. [Stage1 写入说明](./STAGE1_WRITING_GUIDE.md)
   先理解整条启动链为什么从 `stage1` 开始，以及 boot sector 为什么只能做最小事情。
2. [Boot 寄存器小白说明](./BOOT_REGISTERS_BEGINNER.md)
   这是辅助材料，作用是先把后面汇编里反复出现的寄存器名字弄明白。
3. [Stage2 保护模式说明](./STAGE2_PROTECTED_MODE_GUIDE.md)
   这一篇开始进入真正的第二阶段初始化：A20、E820、GDT、保护模式。
4. [E820 逐行讲解版](./E820_LINE_BY_LINE_GUIDE.md)
   它不是独立阶段，而是 Stage2 里最关键、最容易看晕的一小段深挖。
5. [页表 + Long Mode 小白说明](./LONG_MODE_GUIDE.md)
   当你已经懂了保护模式，再继续看为什么还要有页表、PAE、LME 和 long mode。
6. [从 Long Mode 到 C++ 内核](./KERNEL_ENTRY_GUIDE.md)
   最后再看 bootloader 怎么把控制权交给真正的 64 位 C++ 内核。
7. [从 E820 到第一版页分配器](./E820_PAGE_ALLOCATOR_GUIDE.md)
   最后再看内核怎样把 BIOS 给的内存地图变成真正可分配的 4 KiB 页。
8. [从物理页到页表管理器](./KERNEL_PAGING_GUIDE.md)
   再继续看页分配器怎么和页表管理器接起来，真正映射一个新的虚拟页。
9. [从页表管理器到 IDT + 内核堆](./KERNEL_IDT_HEAP_GUIDE.md)
   再往前一步，把“映射页”提升成“能抓异常、能分配堆内存”的真正内核基础设施。
10. [从最小 IDT 到通用 Trap + 可释放堆](./KERNEL_TRAP_HEAP_UPGRADE_GUIDE.md)
   再往前一步，把“最小异常处理 + bump 堆”升级成“通用 trap 框架 + 能 free 的正式堆”。
11. [从通用 Trap 到 PIC + PIT + 定时器中断](./KERNEL_TIMER_IRQ_GUIDE.md)
   再继续往前，把 CPU 异常体系扩成真正能接外部硬件时钟的第一版 IRQ 框架。
12. [从 timer tick 到最小 wait / sleep 接口](./KERNEL_TIMER_SLEEP_GUIDE.md)
   再继续往前，把“会计数的 tick”变成“内核真的能等一段时间再继续”的最小时间接口。
13. [从最小 wait / sleep 到键盘 IRQ](./KERNEL_KEYBOARD_IRQ_GUIDE.md)
   再继续往前，把“只会按时间醒来”的内核扩成“能响应外部输入事件”的内核。
14. [从键盘 IRQ 到最小字符输入](./KERNEL_KEYBOARD_CHAR_INPUT_GUIDE.md)
   再继续往前，把“只会记扫描码”的内核扩成“能真正读出字符”的最小输入系统。
15. [从最小字符输入到控制台行输入](./KERNEL_CONSOLE_INPUT_GUIDE.md)
   再继续往前，把“能读字符”的内核扩成“能回显并读完整一行”的最小控制台。
16. [从控制台行输入到最小 Shell](./KERNEL_SHELL_GUIDE.md)
   再继续往前，把“能读整行”的内核扩成“能执行内建命令”的最小交互系统。
17. [从最小 Shell 到可观察命令 + 带参数命令](./KERNEL_SHELL_EXPANSION_GUIDE.md)
   再继续往前，把“只会认固定单词”的 shell 扩成“能查更多内核状态、还能处理参数”的第一版命令行。
18. [从带参数命令到命令历史](./KERNEL_SHELL_HISTORY_GUIDE.md)
   再继续往前，把“执行完就忘”的 shell 扩成“能保存最近命令历史”的第一版交互系统。
19. [从命令历史到行编辑 + 历史浏览 + 内核观察命令](./KERNEL_SHELL_EDITOR_INSPECT_GUIDE.md)
   再继续往前，把“只会记历史”的 shell 扩成“能编辑、能回看、还能观察启动链状态”的真正调试终端骨架。
20. [从可释放堆到对象分配 + 更正式的内核内存子系统](./KERNEL_MEMORY_OBJECT_GUIDE.md)
   这一步重新回到 `memory/`，把“只会分原始字节”的堆提升成“后面模块能正式调用的 kmalloc/knew 层”。
21. [从对象分配到原始 Boot Volume + 块设备入口](./KERNEL_BOOT_VOLUME_GUIDE.md)
   这一步开始进入“存储”方向，但先不硬写控制器驱动，而是先把 stage2 预读的连续扇区区间变成内核里的原始块设备入口。
22. [从原始块设备到第一版只读文件系统](./KERNEL_FILESYSTEM_GUIDE.md)
   再继续往前，把“只能按扇区读”提升成“能挂载、能走路径、能列目录、能读文件”的第一版只读文件系统。
23. [从只读文件系统到内核文件句柄层](./KERNEL_FILE_HANDLE_GUIDE.md)
   再继续往前，把“shell 直接碰 inode”改成“open/read/close/stat 文件句柄接口”，为后面的 VFS 和文件描述符打基础。
24. [从文件句柄到目录句柄](./KERNEL_DIRECTORY_HANDLE_GUIDE.md)
   再继续往前，把 `ls` 也从“直接读 OS64FS 目录项”改成 `directory_open/read/close`，为 VFS 的目录接口打基础。
25. [从文件/目录句柄到第一版 VFS](./KERNEL_VFS_GUIDE.md)
   再继续往前，把 `ls` / `cat` / `stat` 统一收口到 `vfs_*` 接口，让 shell 不再直接依赖具体文件系统访问层。
26. [从第一版 VFS 到文件描述符表](./KERNEL_FILE_DESCRIPTOR_GUIDE.md)
   再继续往前，把 `cat` 从“直接拿 VFS 文件对象”升级成“先拿 fd 小整数，再用 fd_read/fd_close 操作打开文件”。
27. [从文件描述符表到 shell 当前工作目录](./KERNEL_SHELL_CWD_GUIDE.md)
   再继续往前，让 shell 支持 `pwd` / `cd`，并把相对路径按当前目录解析成绝对路径再交给 VFS。
28. [从 shell cwd 到第一版系统调用形状](./KERNEL_SYSCALL_SHAPE_GUIDE.md)
   再继续往前，把 `open/read/stat/seek/close` 收口成 `sys_*` 入口，为后面的用户态和真正 syscall 指令铺路。
29. [从第一版系统调用形状到 syscall 上下文里的 cwd](./KERNEL_SYSCALL_CWD_GUIDE.md)
   再继续往前，把 `cwd` 从 shell 私有状态抬进 `SyscallContext`，并补上 `sys_getcwd` / `sys_chdir` / `sys_stat_path` / `sys_listdir`。
30. [从 syscall 上下文到第一版 `int 0x80` 软中断入口](./KERNEL_INT80_SYSCALL_GUIDE.md)
   再继续往前，让 CPU 真正执行一次 `int 0x80`，把寄存器打进 IDT 的软中断门，再由 C++ 分发器转到现有 `sys_*`。
31. [从第一版 `int 0x80` 到公开 fd + `sys_write`](./KERNEL_SYSCALL_WRITE_GUIDE.md)
   再继续往前，把公开 syscall fd 先整理成 `0/1/2 = stdin/stdout/stderr`、`3+ = 普通文件`，并补上第一版 `sys_write` 输出路径。
32. [从公开 fd + `sys_write` 到第一版 `stdin/read(0)`](./KERNEL_SYSCALL_STDIN_GUIDE.md)
   再继续往前，把键盘字符缓冲真正接成 `stdin`，让 `read(0, ...)` 和 `int 0x80` 版本都能读到第一版标准输入；后来又继续补到“没字符时线程会 block，键盘 IRQ 到来再 wake”。
33. [从第一版 `stdin/read(0)` 到第一版 `process/thread/scheduler`](./KERNEL_TASKING_GUIDE.md)
   再继续往前，把“整个系统只有一条内核主线在跑”推进成“内核已经有可调度线程、独立线程栈、分优先级的 ready queue、sleep/block/wake 状态和 idle thread”。
34. [从第一版 `process/thread/scheduler` 到“shell 真正跑进调度器”](./KERNEL_SCHEDULER_SHELL_GUIDE.md)
   再继续往前，把“已经存在的调度器骨架”真正接到交互路径上：shell 不再由 `kernel_main` 直接跑，console 等输入时也优先走 keyboard wait queue -> block -> IRQ 唤醒。
35. [从“shell 真正跑进调度器”到第一版 `TSS`](./KERNEL_TSS_GUIDE.md)
   再继续往前，把 long mode 里真正还缺的 `TSS` 补上：内核自己安装一份 GDT、准备 `RSP0` 和 double-fault `IST1`、用 `ltr` 装载 task register，为以后 ring 3 / 内核栈切换铺路。
36. [从第一版 `TSS` 到“每进程地址空间骨架”](./KERNEL_ADDRESS_SPACE_GUIDE.md)
   再继续往前，把“以后用户态到底活在哪份页表里”这件事先立住：支持克隆当前页表根、为新地址空间额外挂用户页，并把地址空间对象挂进 PCB。
37. [从“每进程地址空间骨架”到“第一次真正进入用户态”](./KERNEL_USER_MODE_GUIDE.md)
   再继续往前，第一次真正准备用户代码页、用户栈页和 `iretq` 入口帧，真的落进 ring 3，再让用户代码用 `int 0x80` 打回内核。
38. [从“第一次真正进入用户态”到“第一版 scheduler-managed user thread”](./KERNEL_USER_THREAD_GUIDE.md)
   再继续往前，把那次 `kernel_main` 亲自做的一次性 user mode smoke，升级成真正挂在 scheduler 下面的一条 user thread，并让 `exit` 正式回到调度器。
39. [从“第一版 scheduler-managed user thread”到“每进程 syscall context / fd 视图”](./KERNEL_PROCESS_SYSCALL_CONTEXT_GUIDE.md)
   再继续往前，把还挂在全局变量上的 syscall/fd 视图真正挂进 PCB，让 ring 3 代码通过“当前线程所属进程”拿到自己的 cwd、相对路径和文件句柄视图。
40. [从“每进程 syscall context / fd 视图”到“正式 UserTrapFrame + 每用户线程内核进入栈 + user yield/resume”](./KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md)
   再继续往前，把“能进 ring 3、也能拿到自己 syscall 视图”的 user thread，推进成“能在 syscall 里主动 yield、切去别的线程、再恢复回 ring 3 继续跑”，并正式把 trap frame 和 `TSS.rsp0` 这层机器现场做成可解释的数据结构。

一句话记忆这个顺序：

```text
stage1
-> 先补寄存器基础
-> stage2 保护模式
-> 深挖 E820
-> 页表和 long mode
-> 进入 C++ 内核
-> 第一版页分配器
-> 页表管理器
-> IDT + page fault + 第一版内核堆
-> 通用 trap 框架 + 可释放堆
-> PIC + PIT + timer tick
-> tick 驱动的最小 wait / sleep
-> keyboard IRQ
-> keyboard char input
-> console line input
-> minimal shell
-> observable shell + command arguments
-> shell command history
-> shell line editor + history browse + inspect commands
-> kernel object allocator + formal memory subsystem
-> raw boot volume + block device entry
-> first read-only filesystem
-> kernel file handle layer
-> kernel directory handle layer
-> first VFS layer
-> file descriptor table
-> shell cwd + relative paths
-> first syscall facade
-> cwd inside syscall context
-> first int 0x80 syscall gate
-> public syscall fd 0/1/2 + first sys_write
-> first stdin/read(0) from keyboard char stream
-> stdin block/wakeup on keyboard IRQ
-> first process/thread/scheduler skeleton with priority + sleep/wake
-> shell as a real scheduler-managed kernel thread
-> first kernel-owned GDT + TSS + RSP0/IST1
-> first per-process address-space skeleton + cloned page-table root
-> first real ring 3 entry via iretq + int 0x80 back to kernel
-> first scheduler-managed user thread with exit back to scheduler
-> first per-process syscall context and fd view for user processes
-> formal UserTrapFrame + dedicated per-user-thread kernel-entry stack + user yield/resume path
```
