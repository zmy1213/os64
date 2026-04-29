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
```
