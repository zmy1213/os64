# 小白版源码阅读地图

这份文档不是讲“怎么构建”，而是讲：

> 这个仓库每一层代码到底在干什么，你应该先看谁、后看谁。

如果你现在看到 `boot/`、`kernel/`、`user/` 一堆目录就头大，先看这份。

## 1. 先记住整条主线

这个项目当前最重要的一条链，不是所有功能一起看，而是先看这一条：

```text
BIOS
-> boot/stage1.asm
-> boot/stage2.asm
-> kernel/boot/boot_info.hpp
-> kernel/core/kernel_main.cpp
-> kernel/storage/boot_volume.*
-> kernel/storage/block_device.*
-> kernel/fs/os64fs.*
-> kernel/fs/file.* / directory.*
-> kernel/fs/vfs.*
-> kernel/fs/fd.*
-> kernel/syscall/*
-> kernel/shell/*
```

一句话解释这条链：

- `stage1`：BIOS 先执行它，它只负责最小启动和把 `stage2` 读进来。
- `stage2`：还在比较底层的环境里，负责开 A20、拿 E820、切保护模式、开 long mode、把内核和 boot volume 读到内存。
- `BootInfo`：这是 `stage2` 交给 64 位内核的“启动说明书”。
- `kernel_main.cpp`：64 位内核总入口，后面几乎所有子系统都从这里被初始化和烟测。
- `BootVolume`：把 `stage2` 预读进内存的一段连续扇区，包装成“一个最小原始卷”。
- `BlockDevice`：把“数据来自哪里”抽象掉，让文件系统以后只面对统一的按扇区读写接口。
- `OS64FS`：真正的文件系统，负责 superblock、inode、目录项、路径解析、文件数据读写。
- `file` / `directory`：把 inode 级接口包成“打开文件句柄”和“打开目录句柄”。
- `VFS`：继续做一层统一包装，让更上层不直接依赖 `OS64FS` 的磁盘格式。
- `fd`：把打开文件句柄再包成 `0`、`1`、`2` 这种小整数文件描述符。
- `syscall`：把上面这些能力整理成用户态能调用的接口。
- `shell`：把这些接口变成你在 `os64>` 里实际敲的命令。

## 2. 按目录看，每层负责什么

### `boot/`

- `boot/stage1.asm`
  512 字节 boot sector。它最重要的任务不是“做很多事”，而是“先活下来，然后把 stage2 读出来”。
- `boot/stage2.asm`
  真正的启动大头都在这里。你如果想明白“CPU 怎么从 BIOS 走到 64 位 C++ 内核”，这就是最关键文件。

### `kernel/boot/`

- `boot_info.hpp`
  定义 `stage2 -> kernel` 的交接结构。你可以把它理解成启动期参数包。
- `segments.hpp`
  放段选择子之类的常量，给 long mode / TSS / ring3 代码共用。
- `entry64.asm`
  64 位内核汇编入口，负责从汇编过渡到 C++ 的 `kernel_main`。

### `kernel/memory/`

- `page_allocator.*`
  从 E820 可用内存里切 4 KiB 物理页。
- `paging.*`
  建页表、做虚拟地址映射。
- `heap.*`
  在页表和物理页之上，继续做更好用的堆分配器。
- `kmemory.*`
  再往上做 `kmalloc` / `kfree` / `knew` 这层更像正常内核 API 的接口。
- `address_space.*`
  每个进程自己的页表根和用户地址空间骨架。

### `kernel/interrupts/`

- `interrupts.*`
  IDT、异常入口、trap 分发。
- `pic.*`
  老式 PIC 控制器初始化。
- `pit.*`
  定时器中断来源。
- `keyboard.*`
  键盘扫描码输入。
- `interrupt_stubs.asm`
  汇编级中断入口桩，负责把 CPU 现场整理后交给 C++。

### `kernel/storage/`

- `boot_volume.*`
  把 stage2 预读卷变成可按扇区访问的原始卷对象。
- `block_device.*`
  把原始卷再抽象成统一块设备接口。

如果你想知道“为什么文件系统还没写 ATA 驱动却已经能 `cat` 文件”，重点看这两个文件。

### `kernel/fs/`

- `os64fs.*`
  真正的文件系统实现，最底层、最核心。
- `file.*`
  打开普通文件后的句柄层。
- `directory.*`
  打开目录后的句柄层。
- `vfs.*`
  统一文件系统接口层。
- `fd.*`
  小整数文件描述符层。

如果你想知道下面几个问题，重点看这里：

- `ls` 为什么能列目录
- `cat` 为什么能读文件
- `stat` 为什么能看到 inode / block 信息
- 为什么内核上层越来越少直接碰 `Os64FsInode`

### `kernel/task/`

- `scheduler.*`
  调度器，负责线程切换、sleep/block/wake、时间片轮转。
- `context_switch.asm`
  真正切换上下文的汇编部分。
- `elf_loader.*`
  把 ELF 用户程序装进用户地址空间。
- `user_mode.hpp`
  用户态进入/返回涉及的结构和辅助声明。

### `kernel/syscall/`

- `syscall.*`
  把 VFS、fd、stdin/stdout、cwd 等能力整理成用户态接口。

### `kernel/shell/`

- `shell.*`
  命令解析、命令执行、输出、history、行编辑等交互逻辑。

### `kernel/core/`

- `kernel_main.cpp`
  当前仓库最大的总控文件。
  它不是单纯“入口函数”，而是：
  初始化 + 烟测 + 日志 + shell 启动 的总装配现场。

如果你第一次看它就晕，很正常。正确姿势不是从第 1 行硬啃到第 5000 行，而是先知道里面大致分成哪些区块：

- 串口/VGA 输出工具函数
- 字符串/数字小工具
- boot info / E820 / 内存初始化
- TSS / paging / heap / kmemory 烟测
- boot volume / filesystem / file / directory / vfs / fd 烟测
- syscall / int80 / stdin 烟测
- timer / scheduler / keyboard / console / shell 烟测
- 最后把 shell 作为线程交给 scheduler 跑起来

## 3. 你如果只想先看“文件系统为什么能工作”

按这个顺序看最顺：

1. `boot/stage2.asm`
   看 `load_boot_volume_from_disk`
2. `kernel/boot/boot_info.hpp`
   看 `BootInfo` 里 boot volume 那几个字段
3. `kernel/storage/boot_volume.hpp`
4. `kernel/storage/block_device.hpp`
5. `kernel/fs/os64fs.hpp`
6. `kernel/fs/file.hpp`
7. `kernel/fs/directory.hpp`
8. `kernel/fs/vfs.hpp`
9. `kernel/fs/fd.hpp`
10. `kernel/shell/shell.cpp`
    再回头看 `ls` / `cat` / `stat` 命令怎么调上面这些层

这条阅读顺序的核心思想是：

> 先看“数据从哪里来”，再看“怎么解释成文件系统”，最后看“怎么变成命令”。

## 4. 你如果只想先看“用户程序怎么跑起来”

按这个顺序看：

1. `kernel/task/elf_loader.*`
2. `kernel/memory/address_space.*`
3. `kernel/task/scheduler.*`
4. `kernel/syscall/syscall.*`
5. `kernel/core/kernel_main.cpp`
   重点找这些函数：
   `run_user_mode_smoke_test`
   `run_user_file_program_smoke_test`
   `run_user_elf_program_smoke_test`
   `run_scheduler_elf_thread_smoke_test`

## 5. 你现在最适合的读法

如果你真的是“小白模式”，不要一开始追求“全部一下看懂”。

推荐这个节奏：

1. 先看 `boot/stage1.asm` 和 `boot/stage2.asm`
2. 再看这次我补注释比较多的存储/文件系统接口层
3. 再读 `docs/README.md` 里那条顺序文档链
4. 最后才回去啃 `kernel/core/kernel_main.cpp`

因为 `kernel_main.cpp` 是“把所有东西串起来的地方”，不是“最适合第一次入门的地方”。

## 6. 一句最短总结

这个仓库不是“很多孤立文件”，而是这 4 层不断往上包：

```text
硬件/启动
-> 内存/中断/调度基础设施
-> 存储/文件系统/系统调用
-> shell 和用户程序
```

你每次看不懂时，都先问自己一句：

> 我现在看到的这一层，下面依赖谁，上面又服务谁？

这样比死记函数名更容易真的看懂。
