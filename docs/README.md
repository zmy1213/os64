# 文档索引与学习路径

## 当前版本：先读这条路

1. **[从零开始的主教程](./BEGINNER_TUTORIAL.md)**：宿主/客体、下载安装、第一次启动、备份与工具更新，再从真实函数读到用户态编辑器。没有基础从这里开始。
2. [当前源码阅读地图](./BEGINNER_SOURCE_MAP.md)：按你观察到的现象找文件，不从 kernel_main 第 1 行硬读。
3. [进程运行时](./PROCESS_RUNTIME.md)：argv、spawn/wait/退出回收、直接映射与物理页、brk、ELF/NX、64 KiB 栈与 guard 的精确边界。
4. [持久化存储](./PERSISTENT_STORAGE.md)：RAM/ATA、可写 OS64FS v3、失败回滚、容量限制与已验证/未保证的行为。
5. [用户程序与编辑器](../user/README.md)：edit 完整命令演练、内存库、用户 ABI；[内核目录说明](../kernel/README.md) 补模块关系。

根目录 [README](../README.md) 是快速使用入口。第一次 `make build` 创建数据盘；已有数据盘更新 `/bin` 时先关闭 QEMU，再 `make update-tools`，不要把 `reset-data` 当普通更新。完整回归入口是 `make test`。

可分四次学习：先启动/保存，再理解启动/地址，再实验内存/用户错误，最后追编辑器保存到 ATA。主教程每章都有对应命令、期望结果与排错说明。

## 历史专题：用来解释原理和开发步骤

下面保留原专题正文，并按主题列出阅读顺序。每篇开头都标明其阶段和当前差别；它们没有全部重写成当前源码的逐行注释。正文的“这一轮”“当前”属于当时阶段，历史输出数字和代码片段不可直接套用到现版本。

尤其注意：内核堆从旧 4–8 MiB 移到 16–20 MiB；用户物理页不再限制旧 1–2 MiB 池；用户栈不再只有一页；ELF 不再只有 4 KiB；文件系统不再只是只读 RAM 卷；进程已拥有独立 cwd/fd 并支持运行与回收。当前值以主教程/runtime/源码为准。

历史 [启动到用户态总讲解](./BOOT_TO_USERLAND_WALKTHROUGH.md) 保留最初路线和开发记录。当前仍是自写 BIOS 软盘启动，不是 GRUB/ISO；stage2 按构建生成的布局加载 kernel.bin，不解析内核 ELF。

### 启动与机器基础

1. [Stage1 写入说明](./STAGE1_WRITING_GUIDE.md)
2. [Boot 阶段寄存器小白说明](./BOOT_REGISTERS_BEGINNER.md)
3. [Stage2 进入保护模式说明](./STAGE2_PROTECTED_MODE_GUIDE.md)
4. [E820 逐行讲解版](./E820_LINE_BY_LINE_GUIDE.md)
5. [页表 + Long Mode 小白说明](./LONG_MODE_GUIDE.md)
6. [从 Long Mode 到 C++ 内核](./KERNEL_ENTRY_GUIDE.md)

### 内存与异常的早期设计

1. [从 E820 到第一版页分配器](./E820_PAGE_ALLOCATOR_GUIDE.md)
2. [从物理页到页表管理器](./KERNEL_PAGING_GUIDE.md)
3. [从页表管理器到 IDT + 内核堆](./KERNEL_IDT_HEAP_GUIDE.md)
4. [从最小 IDT 到通用 Trap + 可释放堆](./KERNEL_TRAP_HEAP_UPGRADE_GUIDE.md)
5. [从可释放堆到对象分配 + 更正式的内核内存子系统](./KERNEL_MEMORY_OBJECT_GUIDE.md)

### 时钟、键盘、终端与 Shell

1. [从通用 Trap 到 PIC + PIT + 定时器中断](./KERNEL_TIMER_IRQ_GUIDE.md)
2. [从 timer tick 到最小 wait / sleep 接口](./KERNEL_TIMER_SLEEP_GUIDE.md)
3. [从最小 wait / sleep 到键盘 IRQ](./KERNEL_KEYBOARD_IRQ_GUIDE.md)
4. [从键盘 IRQ 到最小字符输入](./KERNEL_KEYBOARD_CHAR_INPUT_GUIDE.md)
5. [从最小字符输入到控制台行输入](./KERNEL_CONSOLE_INPUT_GUIDE.md)
6. [从控制台行输入到最小 Shell](./KERNEL_SHELL_GUIDE.md)
7. [从最小 Shell 到可观察命令 + 带参数命令](./KERNEL_SHELL_EXPANSION_GUIDE.md)
8. [从带参数命令到命令历史](./KERNEL_SHELL_HISTORY_GUIDE.md)
9. [从命令历史到行编辑 + 历史浏览 + 内核观察命令](./KERNEL_SHELL_EDITOR_INSPECT_GUIDE.md)

### 文件与系统调用的逐层包装

1. [从对象分配到原始 Boot Volume + 块设备入口](./KERNEL_BOOT_VOLUME_GUIDE.md)
2. [从原始块设备到第一版只读文件系统](./KERNEL_FILESYSTEM_GUIDE.md)
3. [从只读文件系统到内核文件句柄层](./KERNEL_FILE_HANDLE_GUIDE.md)
4. [从文件句柄到目录句柄](./KERNEL_DIRECTORY_HANDLE_GUIDE.md)
5. [从文件/目录句柄到第一版 VFS](./KERNEL_VFS_GUIDE.md)
6. [从第一版 VFS 到文件描述符表](./KERNEL_FILE_DESCRIPTOR_GUIDE.md)
7. [从文件描述符表到 shell 当前工作目录](./KERNEL_SHELL_CWD_GUIDE.md)
8. [从 shell cwd 到第一版系统调用形状](./KERNEL_SYSCALL_SHAPE_GUIDE.md)
9. [从第一版系统调用形状到 syscall 上下文里的 cwd](./KERNEL_SYSCALL_CWD_GUIDE.md)
10. [从 syscall 上下文到第一版 `int 0x80` 软中断入口](./KERNEL_INT80_SYSCALL_GUIDE.md)
11. [从第一版 `int 0x80` 到公开 fd + `sys_write`](./KERNEL_SYSCALL_WRITE_GUIDE.md)
12. [从公开 fd + `sys_write` 到第一版 `stdin/read(0)`](./KERNEL_SYSCALL_STDIN_GUIDE.md)
13. [从第一版只读文件系统到带位图和一致性校验的 OS64FS v3](./KERNEL_OS64FS_V3_GUIDE.md)

### 任务、用户态和上下文切换

1. [从第一版 `stdin/read(0)` 到第一版 `process/thread/scheduler`](./KERNEL_TASKING_GUIDE.md)
2. [从第一版 `process/thread/scheduler` 到“shell 真正跑进调度器”](./KERNEL_SCHEDULER_SHELL_GUIDE.md)
3. [从“shell 真正跑进调度器”到第一版 `TSS`](./KERNEL_TSS_GUIDE.md)
4. [从第一版 `TSS` 到“每进程地址空间骨架”](./KERNEL_ADDRESS_SPACE_GUIDE.md)
5. [从“每进程地址空间骨架”到“第一次真正进入用户态”](./KERNEL_USER_MODE_GUIDE.md)
6. [从“第一次真正进入用户态”到“第一版从文件系统加载用户程序”](./KERNEL_USER_PROGRAM_LOADER_GUIDE.md)
7. [从“第一版从文件系统加载用户程序”到“第一版 ELF 用户程序装载器”](./KERNEL_USER_ELF_LOADER_GUIDE.md)
8. [从“第一次真正进入用户态”到“第一版 scheduler-managed user thread”](./KERNEL_USER_THREAD_GUIDE.md)
9. [从“第一版 scheduler-managed user thread”到“scheduler 正式接管 ELF 用户线程”](./KERNEL_SCHEDULER_ELF_THREAD_GUIDE.md)
10. [从“第一版 scheduler-managed user thread”到“每进程 syscall context / fd 视图”](./KERNEL_PROCESS_SYSCALL_CONTEXT_GUIDE.md)
11. [从“每进程 syscall context / fd 视图”到“正式 UserTrapFrame + 每用户线程内核进入栈 + user yield/resume”](./KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md)
12. [从“正式 UserTrapFrame + user yield/resume”到“第一版 user timer preemption”](./KERNEL_USER_TIMER_PREEMPT_GUIDE.md)
13. [从“第一版 user timer preemption”到“调度器保存内核上下文时也保存 CR3”](./KERNEL_SCHEDULER_CR3_SWITCH_GUIDE.md)
14. [从“调度器保存内核上下文时也保存 CR3”到“用户态 `read(0)` 真正 block/wake”](./KERNEL_USER_STDIN_BLOCK_GUIDE.md)

## 读一篇专题时的三个问题

1. 输入来自哪里？例如 BIOS 的 BootInfo，用户传来的指针，还是设备的状态寄存器。
2. 它改变谁的状态？例如物理页位图、当前进程页表、线程等待队列，还是磁盘元数据。
3. 失败后怎么办？例如返回错误、撤销新页、回滚扇区、结束用户进程，还是内核停机。

先用自己能运行的实验回答这三问，再回看汇编细节；不要为了照着历史例题重建目录或覆盖现有文件。
