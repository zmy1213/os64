# kernel 源码入口

这里说明当前内核的分工。零基础先读 [当前主教程](../docs/BEGINNER_TUTORIAL.md)；需要精确的用户内存/回收语义读 [PROCESS_RUNTIME.md](../docs/PROCESS_RUNTIME.md)，需要磁盘故障边界读 [PERSISTENT_STORAGE.md](../docs/PERSISTENT_STORAGE.md)。旧的“第一版”专题是开发阶段记录。

## 先追一条可观察的链

```text
boot/stage2.asm 传 BootInfo
→ boot/entry64.asm 设置内核栈并调用 kernel_main
→ 初始化内存/异常/文件接口/时钟/调度/键盘，执行 RAM fixture 回归
→ 挂载 ATA 正式数据卷（失败回退 RAM）
→ 安装 syscall 服务，创建 Shell 进程/线程
→ Shell run 装载独立用户 ELF
→ ring 3 int80 请求服务/退出
→ 等待、回收，再回 Shell
```

`core/kernel_main.cpp` 很长，因为同时保留了最早的 smoke 与当前初始化。第一次不要从头硬读；在实际操作后按下表搜索函数，再看它依赖的接口。

## 模块地图

| 目录 | 当前职责 | 第一遍先读 |
| --- | --- | --- |
| boot | ELF 链接布局、64 位入口、BootInfo 和段选择子 | entry64.asm、boot_info.hpp |
| core | 初始化、自测、错误诊断、正式数据盘与 Shell 装配 | kernel_main、start_kernel_shell_under_scheduler |
| memory | E820 物理页双位图、直接映射、私有页表、内核堆 | page_allocator.hpp、paging.hpp、address_space.hpp |
| interrupts | IDT/GDT/TSS、异常/IRQ/int80 汇编入口、PIC/PIT、PS/2 与串口 | interrupts.hpp、interrupt_stubs.asm |
| task | PCB/TCB、ready/sleep/block/wake、用户启动/退出/回收 | scheduler.hpp、elf_loader.hpp |
| syscall | 用户指针检查、当前进程上下文、编号分发与服务 | syscall.hpp、kernel_handle_syscall |
| storage | RAM BootVolume、统一 BlockDevice、ATA PIO/flush | block_device.hpp、ata_pio.hpp |
| fs | OS64FS v3 读写/事务与 inode，文件/目录句柄、VFS、fd | os64fs.hpp、vfs.hpp、fd.hpp |
| shell | 命令解析、cwd、文件修改、run/wait/reap、观察和电源命令 | handle_run_command、shell_run_once |
| console | VGA 输出和内核交互行编辑 | console.hpp |
| runtime | 无宿主 libc 时的基础内存工具 | runtime.hpp |
| cpu | CPUID/x87/SSE、ACPI/MP 拓扑、xAPIC AP 启动、内核锁与每核快照 | cpu.hpp、smp.hpp、topology.hpp |
| log / perf | 有界结构化日志、CPU/内存/调度快照 | log.hpp、perf.hpp |
| device / net | PCI 配置空间、virtio DMA ring、ARP/IPv4/ICMP/UDP | pci.hpp、network.hpp |

## 内存：三个分配层不要混用

1. `alloc_page/free_page` 分配/归还整张物理 4 KiB 页。物理页位图最高管理 256 MiB，E820 usable 范围和保留区决定哪些可以分配。
2. `paging_initialize_direct_map` 建 `0xffff800000000000 + physical` 的 supervisor 访问窗口，`map_page_in_root` 建指定虚拟→物理映射；CR3/PTE 仍保存物理地址。C++ 访问物理页内容用 `paging_physical_pointer`。
3. `heap_alloc/heap_free` 与 `kmalloc/kfree` 管 16–20 MiB 内核堆中的小对象。固定堆容量可复用，不在每次 kfree 时归还整个物理页。

低 1 MiB 保留给启动器、内核文件、启动卷和设备布局；BSS 另放在物理 `0x100000`–`0x160000` 的保留窗口，entry64 显式清零。**bootstrap 内核栈另在物理 `0x170000`–`0x180000` 保留 64 KiB**。不能把这些内核状态页误发给用户堆。

`address_space` 管每个进程的私有页表。克隆会复制页表树，共享内核 supervisor 物理页，用户页面按新程序重新装载；不是 fork/COW。destroy 释放拥有的用户数据与页表。unmap 回收用户叶页及空私有层级，当前映射失效时刷新相应 TLB 项。

用户 brk 与内核堆不同：进程在 4–8 MiB 用户窗口内立即分配，ELF 后开始，到 `0x7ef000` 为止；栈 `0x7f0000`–`0x800000`，中间一页 guard 不映射。支持 NX 的 CPU 上数据/堆/栈不可执行。这里没有 demand paging。

## 任务和中断：上下文由谁保存

`scheduler.cpp` 创建进程/线程，决定谁 ready、谁等待以及何时回收。`context_switch.asm` 保存暂停内核调用所需的寄存器、RSP、RFLAGS，并切 CR3；`interrupt_stubs.asm` 保存用户 IRQ/syscall/异常的完整寄存器现场，最终 iretq 回用户态。不能只看切换汇编保存少量寄存器，就判断用户抢占没有保存其他寄存器。

用户程序可以被 PIT timer 抢占；内核仍在明确调度点切换。IRQ0 在可能切换之前发 PIC EOI，避免暂停的 IRQ 处理器阻塞后续时钟。TSS.rsp0 随当前线程指向专用进入栈。用户线程还有独立的 bootstrap/resume 栈，避免反复 int80 覆盖最初返回现场。

最后一条线程退出立即关闭文件/管道/UDP，唤醒等待者；`scheduler_reap_process` 清理 fd、页/页表、内核栈、PCB/TCB。孤儿自动回收在另一线程栈上进行。切换汇编在关闭中断的临界区保存/恢复各线程 x87/SSE 的 512 字节 FXSAVE 现场；当前最多四核：用户线程首次选较轻核心后固定，内核线程只在 BSP，大内核锁串行保护共享状态。每核 TSS/GDT/IST、current/idle/时间片/FX 独立；全局 tick 仅 BSP 增加。AVX/XSAVE、用户线程 API、迁移/跨核共享地址空间/TLB shootdown 尚未实现。

正式等待使用 Sleeping/Blocked 后切栈，user/idle 才放锁；不能持锁 HLT 等别核的事件。启动历史 CPU 记账自测有独立 Running helper，仅在 AP 启动前运行。见 [AP 启动](../docs/SMP_BOOT_TUTORIAL.md) 与 [多核调度](../docs/SMP_SCHEDULER_TUTORIAL.md)。

## 系统调用：服务存在不等于任何指针都可用

`kernel_handle_syscall` 识别用户来源，`user_syscall_arguments_valid` 检查每页 present/user/writable 与字符串结束，`current_dispatch_context` 取当前线程所属进程的 fd/cwd/output 视图。`dispatch_syscall_registers` 把 RAX 编号与 RDI/RSI/RDX/RCX/R8 转交服务。

当前有文件/目录/终端操作、exit/yield/sleep/getpid/spawn/waitpid、brk，以及单次 replace_file。stdin 等待能阻塞并被键盘/串口输入唤醒。用户异常走结束当前进程路径；内核异常仍诊断并停机。

## 存储：两个后端与一个格式

`boot_volume` 是 stage2 预读的固定 RAM fixture，供原里程碑测试使用。正式交互探测 ATA primary master：`ata_pio` 实现 IDENTIFY、512 字节单扇区 PIO、FLUSH CACHE；`block_device` 给 RAM/ATA 同样的接口。没有可挂载数据盘时使用 RAM 回退，日志明确标 volatile。

`os64fs` 已是**可读写的 v3**，不是旧 v1 只读实现。`mutate` 暂存有界扇区变更和缓存快照，校验后写入/flush，运行时错误尝试回滚；不可恢复则取消挂载。没有磁盘日志、断电恢复或自动 fsck。

`file`/`directory` 提供带位置的句柄，`vfs` 提供统一接口，`fd` 给每进程小整数句柄。`sys_replace_file` 将编辑器的完整保存交给一次事务，避免 TRUNC 与 write 拆成两笔操作。硬链接、符号链接、文件权限与现代块设备驱动不在当前范围。

## 修改前怎样验证

参考主 README 的测试命令。新内存测试分宿主位图 sanitizer 与真实 QEMU 用户流程；存储测试同时有宿主故障注入和冷启动持久化。普通 build/clean 保留用户数据盘；更新用户工具使用停止 QEMU 后的 `make update-tools`，不要用 reset-data 代替更新。
