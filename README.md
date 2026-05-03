# os64

这个项目现在明确改成：

> 不用 `Limine`，不用现成 bootloader，从零开始自己把一个 `x86_64` 内核启动起来。

这条路比用 `Limine` 难很多，但如果你的目标是“真正理解操作系统最底层怎么起来”，这条路更纯。

这份文档不只是列步骤，还会解释：

- 为什么要这样做
- 每一步的输入和输出是什么
- 每一步怎么测试

---

## 0. 怎么启动

如果你现在只是想把这个项目先跑起来，
最短路径直接按下面做。

### 第一步：检查环境

运行：

```bash
./scripts/check-env.sh
```

这一步会检查这个项目当前真正要用到的工具：

- `nasm`
- `qemu-system-x86_64`
- `clang++` 或 `g++`
- `ld.lld` 或 `x86_64-elf-ld`
- `dd` / `truncate` / `hexdump`

如果这里没过，
后面的构建和启动基本都会失败。

### 第二步：先构建镜像

运行：

```bash
make stage1
```

这一步会做的事情是：

1. 汇编 `stage1.asm`
2. 汇编 `stage2.asm`
3. 编译并链接 64 位内核
4. 生成 `build/disk.img`

你可以把它理解成：

> 把“bootloader + kernel”真正打包成一张 QEMU 能启动的原始磁盘镜像。

### 第三步：图形界面启动

如果你想看到 BIOS / VGA 文本界面，
运行：

```bash
make run-stage1-gui
```

这会打开一个 QEMU 窗口，
你能直接看到屏幕上的启动过程和 shell 提示符。

### 第四步：终端里启动

如果你更关心串口日志，
想直接在终端里看输出，
运行：

```bash
make run-stage1
```

这个模式不会弹图形窗口，
会把串口输出直接打印到当前终端。

### 第五步：自动测试

正常启动链测试：

```bash
make test-stage1
```

非法指令异常测试：

```bash
make test-invalid-opcode
```

页错误异常测试：

```bash
make test-page-fault
```

这几个测试都会自动构建镜像、启动 QEMU、抓串口日志，再检查关键输出。

现在异常测试和正常启动测试都不再靠固定 `sleep 2` / `sleep 3` 盲等。

原因是内核功能越来越多以后，
镜像体积和启动自测链都会变长；
这时固定等待时间很容易把“还在正常启动”误判成“测试失败”。

所以现在测试脚本会在一个有限超时窗口里轮询串口日志：

- 关键里程碑一旦已经出现，就提前收尾
- 如果直到超时都没出现，才真正判失败

### 第六步：清理构建产物

如果你想删掉 `build/` 重新来，
运行：

```bash
make clean
```

### 启动后你会看到什么

如果一切正常，
启动日志里会看到类似：

- `stage1 ok`
- `stage2 ok`
- `protected mode ok`
- `paging ok`
- `long mode ok`
- `hello from os64 kernel`
- `tss ok`
- `address_space ok`
- `user mode ok`
- `filesystem ok`
- `file_layer ok`
- `directory_layer ok`
- `vfs_layer ok`
- `fd_layer ok`
- `syscall_layer ok`
- `int80_syscall ok`
- `scheduler ok`
- `user thread ok`
- `user_mode_yield_before=1`
- `user_mode_yield_after=1`
- `shell ok`
- `shell_process_pid=6`
- `shell_thread_tid=13`
- `shell_thread_started_tid=13`

调度器这一轮启动时还会额外打印 3 组关键自测日志：

- `sched_priority_trace=HABABC`
- `sched_sleep_trace=ABAB`
- `sched_block_trace=BWb`

它们分别对应：

- 优先级 + 同优先级 round-robin
- sleep + idle 唤醒链路
- block + wake 链路

最后会进入最小交互 shell，
而且现在这个 shell 已经不是 `kernel_main` 直接死循环跑出来的，
而是一个真正交给 scheduler 管理的内核线程，
提示符是：

```text
os64>
```

当前已经支持这些命令：

- `help`
- `mem`
- `ticks`
- `heap`
- `disk`
- `pwd`
- `cd [path]`
- `ls [path]`
- `cat <path>`
- `stat <path>`
- `irq`
- `bootinfo`
- `e820`
- `cpu`
- `uptime`
- `echo <text>`
- `history`
- `clear`

当前输入行也已经支持这些编辑动作：

- `Left` / `Right`
- `Home` / `End`
- `Backspace`
- `Delete`
- `Up` / `Down` 浏览历史

当前 shell 的屏幕配色也做过一轮收敛：

- 普通正文是更柔和的浅灰色
- 提示符 `os64>` 单独用更轻的青色强调

这样做的原因不是“好看优先”，
而是为了让长时间调试时屏幕不那么刺眼，同时又能一眼找到交互入口。

当前内存子系统也已经不只停在“能分原始字节”：

- `PageAllocator` 负责物理页
- `paging` 负责虚拟地址映射
- `KernelHeap` 负责小块堆分配和回收
- `kmalloc` / `kfree` / `kcalloc` 负责正式的内核分配入口
- `knew<T>` / `kdelete<T>` 负责 C++ 对象构造和析构

当前存储方向也已经往前走了一步：

- `stage2` 会先把一小段 `boot volume` 从启动介质读进内存
- `BootInfo` 会把这段原始块区的位置和扇区信息交给 64 位内核
- kernel 里现在已经有 `BootVolume -> BlockDevice -> OS64FS` 这条读路径
- kernel 里又在 `OS64FS` 上面补了 `FileHandle` 文件句柄层
- kernel 里还在 `OS64FS` 上面补了 `DirectoryHandle` 目录句柄层
- kernel 里现在又在文件句柄和目录句柄上面补了第一版 `VFS`
- kernel 里现在还在 `VFS` 上面补了第一版 `FileDescriptorTable` 文件描述符表
- kernel 里现在又在 fd 表上面补了第一版 `SyscallContext`
- `SyscallContext` 现在已经不只记 fd 表，也开始记 `cwd`
- kernel 里现在已经有 `sys_open` / `sys_read` / `sys_stat` / `sys_seek` / `sys_close`
- kernel 里现在又补了 `sys_getcwd` / `sys_chdir` / `sys_stat_path` / `sys_listdir`
- kernel 里现在还补了第一版 `int 0x80` 软中断入口，会把寄存器参数转进现有 `sys_*` 分发器
- kernel 里现在又把公开 syscall fd 先整理成 `0/1/2 = stdin/stdout/stderr`、`3+ = 普通文件`
- kernel 里现在已经补了第一版 `sys_write`，能先把 `stdout/stderr` 写到当前控制台输出路径
- kernel 里现在又把 `stdin` 真正接到了键盘字符流，`sys_read(0, ...)` 和 `int 0x80 read(0, ...)` 都已经能读输入
- 当 `stdin` 当前没有字符时，如果调用者正跑在线程上下文里，内核现在也会把它 block 到 keyboard wait queue，再由键盘 IRQ 唤醒
- kernel 里现在已经补了第一版 `Process/Thread/Scheduler` 骨架，开始真正区分“进程拥有资源”和“线程在 CPU 上执行”
- 每个 kernel thread 现在已经有自己的独立栈，调度器也已经能按 `high/normal/background` 选线程，并在同优先级里 round-robin
- 线程现在已经有 `ready/running/sleeping/blocked/finished` 这些状态，`idle thread` 也已经补上
- `PIT IRQ0` 现在不只会记全局 tick，也会给当前线程记账，并在时间片用完时发出第一版 reschedule 请求
- 当前调度模型还不是“完整抢占式内核”：真正切换先放在 `timer_wait_ticks` / `console_read_line_with_history` / `sys_read(stdin)` 这种安全点里完成
- kernel 里现在已经第一次真正进入 ring 3：内核会克隆一份用户地址空间、映射 1 页用户代码和 1 页用户栈、用 `iretq` 落进用户态，再让用户代码用 `int 0x80` 调回现有 syscall 路径
- 这一步现在还只是教学型 smoke test，不是完整用户进程模型；它主要证明 `ring 0 -> ring 3 -> ring 0` 的链路已经打通
- kernel 里现在又往前走了一步：这次用户态不再只是 `kernel_main` 自己手工调一次，而是已经能作为第一版 `user thread` 挂进 scheduler，由 scheduler 切它进 ring 3，再在 `exit` 后正式把线程回收到 `finished`
- 这一步里专门避开了一个很典型的坑：user thread 的 kernel-resume 栈现在先放在 low identity-mapped 页上，避免切到 cloned user root 之后当前内核栈突然失去映射
- kernel 里现在又把 `FileDescriptorTable + SyscallContext` 正式挂进 `ProcessControlBlock`，让“每进程自己的 cwd/fd/syscall 视图”第一次真正落地
- `int 0x80` 分发现在如果正跑在线程上下文里，会优先取“当前线程所属进程”的 `syscall_context`，不再先信任全局默认上下文
- ring 3 smoke program 现在不只会打印一句话，还会 `getcwd()` 并用相对路径 `open("readme.txt")`；scheduler 版 smoke 还会故意把默认内核 cwd 设成 `/docs`，借此证明用户进程看到的仍然是它自己的 `/`
- shell 里可以用 `disk` 看块设备，用 `pwd` / `cd` 管当前目录，用 `ls` / `cat` / `stat` 看文件系统
- shell 会先把相对路径按 cwd 解析成绝对路径，例如 `cd docs` 后 `cat guide.txt` 会解析成 `/docs/guide.txt`
- 现在 `pwd` / `cd` / `ls` / `cat` / `stat` 这些路径命令都已经开始改成通过 `SyscallContext + sys_*` 这层工作
- 这样 cwd 不再只是 shell 私有变量，而开始变成更像“进程上下文”的内核状态

### 一个很重要的提醒

不要并发跑多个 `make test-*`。

原因不是“脚本写得挑剔”，
而是这些测试都会抢同一个：

```text
build/disk.img
```

并发时很容易出现：

- QEMU 镜像写锁冲突
- 串口日志互相覆盖
- 一个测试读到另一个测试的结果

所以正确做法是：

> 一个一个串行跑。

### 如果你的 QEMU 不在默认位置

这个仓库的 `Makefile` 默认用了：

```text
/opt/homebrew/bin/qemu-system-x86_64
```

如果你机器上的 QEMU 不在这里，
可以这样指定：

```bash
make QEMU=/你的/qemu-system-x86_64 run-stage1-gui
```

或者：

```bash
make QEMU=/你的/qemu-system-x86_64 run-stage1
```

一句话总结：

> 先 `./scripts/check-env.sh`，再 `make run-stage1-gui` 或 `make run-stage1`，要验证就跑 `make test-stage1`。

---

## 1. 先定路线

如果不用 `Limine`，你必须自己解决“谁把内核拉起来”这个问题。

这里我给你选最适合“从零学习”的路线：

> `BIOS -> MBR boot sector -> second stage loader -> 32/64 位切换 -> kernel`

也就是：

1. 先让 BIOS 加载你的第一个扇区
2. 第一个扇区再去加载第二阶段加载器
3. 第二阶段加载器去准备 64 位环境
4. 然后跳到你自己的内核

### 为什么选这条路线

因为它最“从零”。

你会真正碰到：

- 16 位实模式
- BIOS 中断
- MBR 启动扇区
- A20
- GDT
- 保护模式
- 页表
- long mode
- ELF 内核加载

这比直接用 `Limine` 难，但学到的底层知识更多。

### 这条路线的缺点

- 非常底层
- 调试痛苦
- 一开始进展慢
- 很容易被 bootloader 细节拖住

所以你必须严格控制第一阶段目标。

---

## 2. 第一阶段到底做什么

第一阶段不要做“现代内核全家桶”。

只做这件事：

> 让 BIOS 成功加载你自己的 bootloader，bootloader 成功进入 64 位模式，并跳到你的 `kernel_main()`，然后屏幕打印一行字。

第一阶段完成标准：

1. BIOS 能执行你的 MBR
2. 你的 MBR 能加载 second stage
3. second stage 能进入 64 位模式
4. 能跳到你的 kernel
5. kernel 能打印 `hello from os64 kernel`

只要这 5 点打通，这个项目就真正开工了。

---

## 3. 你最终要写哪几块

如果完全不用 `Limine`，第一阶段最少要写 4 块：

### 1. Stage 1 bootloader

特点：

- 16 位实模式
- 只能占一个扇区，通常是 512 字节
- BIOS 直接加载它到 `0x7c00`

职责：

- 设置最小运行环境
- 打印最早期调试信息
- 从磁盘继续加载 second stage

### 2. Stage 2 loader

特点：

- 比 stage 1 大
- 可以分多个扇区
- 负责更复杂的初始化

职责：

- 开启 A20
- 读取内存映射
- 建 GDT
- 进保护模式
- 读入内核
- 建页表
- 进入 long mode
- 跳到 64 位内核入口

### 3. Kernel

特点：

- 64 位代码
- freestanding C++ 为主

职责：

- 初始化控制台
- 接收 boot 信息
- 初始化中断
- 初始化内存管理
- 初始化调度骨架

### 4. Build system

职责：

- 组装 bootloader
- 编译 kernel
- 链接 ELF
- 生成磁盘镜像
- 用 `QEMU` 运行

---

## 4. 从零开始的正确步骤

下面这部分是这次最重要的内容。

## Step 0：先准备工具链

### 你需要什么

1. `nasm`
2. `clang++` 或 `gcc`
3. `ld.lld` 或 `x86_64-elf-ld`
4. `llvm-objcopy` 或 `objcopy`
5. `qemu-system-x86_64`
6. `gdb` 或 `lldb`
7. 常见基础命令：
   - `dd`
   - `truncate`
   - `hexdump`

### 为什么

因为这次不是普通应用程序开发。

你要自己控制：

- 扇区内容
- 入口地址
- 链接地址
- 镜像布局

### 怎么测试

跑环境检查脚本：

```bash
./scripts/check-env.sh
```

通过标准：

- `nasm` 存在
- `qemu-system-x86_64` 存在
- `clang++` 或 `gcc` 存在
- `ld.lld` 或 `x86_64-elf-ld` 存在

环境通过后，文档建议按下面这个顺序看，这样最符合你现在这条启动链的实现顺序：

1. [Stage1 写入说明](./docs/STAGE1_WRITING_GUIDE.md)
   先理解 BIOS 为什么先执行它、它只负责什么、为什么不能把复杂逻辑塞进 512 字节。
2. [Boot 寄存器小白说明](./docs/BOOT_REGISTERS_BEGINNER.md)
   这是辅助文档，先把 `AX`、`BX`、`SP`、`CR0` 这些名字看熟，后面读汇编不会太痛苦。
3. [Stage2 保护模式说明](./docs/STAGE2_PROTECTED_MODE_GUIDE.md)
   开始进入真正的第二阶段：A20、E820、GDT、保护模式。
4. [E820 逐行讲解版](./docs/E820_LINE_BY_LINE_GUIDE.md)
   这是 Stage2 里最容易卡住的一小段，单独拆出来细看最合适。
5. [页表 + Long Mode 小白说明](./docs/LONG_MODE_GUIDE.md)
   看完保护模式后，再理解为什么还要做页表、PAE、LME 和 long mode。
6. [从 Long Mode 到 C++ 内核](./docs/KERNEL_ENTRY_GUIDE.md)
   最后再看 bootloader 怎么把控制权真正交给 `kernel_main.cpp`。
7. [从 E820 到第一版页分配器](./docs/E820_PAGE_ALLOCATOR_GUIDE.md)
   当内核已经能启动后，再继续理解它怎么真正开始管理“哪些物理内存能用”。
8. [从物理页到页表管理器](./docs/KERNEL_PAGING_GUIDE.md)
   再往前一步，理解“拿到物理页”和“把它映射到虚拟地址”其实是两件不同的事。
9. [从页表管理器到 IDT + 内核堆](./docs/KERNEL_IDT_HEAP_GUIDE.md)
   然后再看内核怎么先把异常接住，再把页映射能力提升成真正可用的堆分配。
10. [从最小 IDT 到通用 Trap + 可释放堆](./docs/KERNEL_TRAP_HEAP_UPGRADE_GUIDE.md)
    再继续看内核怎么把最小异常处理扩成统一 trap 框架，并把堆升级成能 `free` 的正式版本。
11. [从通用 Trap 到 PIC + PIT + 定时器中断](./docs/KERNEL_TIMER_IRQ_GUIDE.md)
    再继续看内核怎么第一次真正接外部硬件中断，并得到第一版系统 tick。
12. [从 timer tick 到最小 wait / sleep 接口](./docs/KERNEL_TIMER_SLEEP_GUIDE.md)
    再继续看内核怎么把“只会计数”的 tick 变成真正可用的时间等待能力。
13. [从最小 wait / sleep 到键盘 IRQ](./docs/KERNEL_KEYBOARD_IRQ_GUIDE.md)
    再继续看内核怎么第一次接收外部输入设备发来的中断和扫描码。
14. [从键盘 IRQ 到最小字符输入](./docs/KERNEL_KEYBOARD_CHAR_INPUT_GUIDE.md)
    再继续看内核怎么把扫描码翻译成字符，并放进真正可读的输入缓冲区。
15. [从最小字符输入到控制台行输入](./docs/KERNEL_CONSOLE_INPUT_GUIDE.md)
    再继续看内核怎么把字符回显到 VGA，并把一串按键真正读成一整行输入。
16. [从控制台行输入到最小 Shell](./docs/KERNEL_SHELL_GUIDE.md)
    再继续看内核怎么把整行输入变成真正可执行的内建命令。

如果你想直接在 `docs/` 目录里按顺序读，也可以先看：

- [文档阅读顺序](./docs/README.md)

---

## Step 1：确定第一版磁盘布局

### 你要做什么

先不要搞文件系统。

第一版直接用“原始磁盘布局”：

```text
sector 0        -> stage1 boot sector
sector 1..N     -> stage2 loader
after that      -> kernel binary / kernel ELF
```

### 为什么

这样最简单。

如果你一开始就引入 FAT 文件系统，你会同时面对：

- bootloader
- 文件系统解析
- 内核加载

复杂度太高。

### 输入是什么

- 你的 `stage1.bin`
- 你的 `stage2.bin`
- 你的 `kernel.elf`

### 输出是什么

- 一个 `disk.img`

### 怎么测试

测试点不是“能进内核”，而是：

- `disk.img` 大小正确
- 第一个扇区最后两个字节是 `0x55 0xaa`

可用命令：

```bash
hexdump -C disk.img | head
```

---

## Step 2：写 Stage 1 boot sector

### 你要做什么

写一个 16 位的第一个扇区。

它要完成的工作极少：

1. BIOS 启动后接管控制权
2. 设置寄存器和栈
3. 用 BIOS 中断打印一小段文字
4. 通过 BIOS 磁盘中断读取 second stage
5. 跳过去执行

### 为什么要这么小

因为 BIOS 只会先帮你加载一个扇区。

你能直接拿到的东西非常少。

### 输入是什么

- BIOS 已经把第一个扇区放到 `0x7c00`

### 输出是什么

- second stage 已经被加载到你指定的内存地址
- CPU 跳到了 second stage

### 你会学到什么

- 什么是实模式
- BIOS 启动到底给了你什么
- 为什么一个 boot sector 只有这么小的空间

### 怎么测试

第一轮只测一件事：

> boot sector 能在屏幕上打印 `stage1 ok`

运行方式：

```bash
qemu-system-x86_64 -drive format=raw,file=disk.img,if=floppy,index=0
```

这里要特意加上 `if=floppy`，因为当前这张 `disk.img` 是按 1.44MB 软盘几何参数来做的，
我们的 stage1/stage2 也一直按最基础的 BIOS CHS 软盘方式在读扇区。
如果你把它当成普通硬盘挂载，前面少量扇区可能还能“碰巧读对”，
但一旦跨过软盘和硬盘几何参数开始分叉的位置，后面的扇区就会读错。

通过标准：

- 屏幕出现 `stage1 ok`

第二轮再测：

- 能成功跳到 second stage

---

## Step 3：写 Stage 2 loader

### 你要做什么

second stage 是整个“从零启动”最关键的一段。

它至少要做这些：

1. 开 A20
2. 探测内存
3. 准备 GDT
4. 进入保护模式
5. 加载 kernel
6. 建页表
7. 进入 long mode
8. 跳到 64 位 kernel

### 为什么 stage 1 不做这些

因为 stage 1 空间太小，而且 BIOS 的最早环境太受限。

真正复杂的事情必须放到 second stage。

### 输入是什么

- stage 1 已经把 second stage 读到了内存

### 输出是什么

- CPU 已经具备进入 64 位内核的条件

### 怎么测试

分 4 小步测，不要一次全做完：

1. `stage2 ok`
2. `a20 ok`
3. `protected mode ok`
4. `long mode ok`

每完成一个点，就先停在那并打印，别一口气写到底。

---

## Step 4：开启 A20

### 这是什么

老的 x86 会把地址线做兼容处理，不开 A20 会影响访问 1MB 以上内存。

### 你要做什么

在 second stage 里开启 A20。

### 为什么要做

后面你要：

- 访问更高内存
- 放 second stage
- 放 kernel
- 建页表

不开 A20 很容易内存行为异常。

### 怎么测试

第一版最简单：

- 开 A20 后打印 `a20 ok`

进阶一点：

- 做一个简单地址访问验证

---

## Step 5：获取内存映射

### 你要做什么

用 BIOS `E820` 拿系统内存布局。

### 为什么

内核以后不能假设“所有内存都能用”。

你必须知道：

- 哪些是 usable
- 哪些是 reserved
- 哪些被 BIOS / 设备占用

### 输入是什么

- BIOS 提供的内存信息接口

### 输出是什么

- 一张内存区域表

### 怎么测试

第一版不用太复杂：

- 把 region 数量打印出来
- 打印前几个 region 的起始地址和长度

通过标准：

- 你能看见 usable / reserved 分区信息

---

## Step 6：建立 GDT 并进入保护模式

### 你要做什么

先从 16 位实模式切到 32 位保护模式。

### 为什么不能直接跳 64 位

因为 long mode 的进入需要一套前置条件，通常会先经过更正式的保护模式准备阶段。

### 输入是什么

- 已经在 second stage
- A20 已开
- GDT 已准备

### 输出是什么

- CPU 进入 32 位保护模式

### 怎么测试

第一版通过标准：

- 切到保护模式后还能打印 `protected mode ok`

如果切过去直接黑屏，多半是：

- GDT 错了
- far jump 错了
- 段寄存器没重装

---

## Step 7：加载内核

### 你要做什么

把内核从磁盘读到内存。

### 第一版最简单方案

先不要完整解析复杂文件系统。

可以用两种方法：

1. 直接按固定扇区读取 kernel binary
2. 读取 `kernel.elf` 并只解析最少需要的 ELF 段

### 推荐

如果你真的是从零学，建议：

> 第一版先固定扇区读一个简单内核映像

然后第二版再换成 ELF 加载。

### 为什么

因为 ELF 解析会再增加一层复杂度。

### 输入是什么

- second stage
- 磁盘里的 kernel

### 输出是什么

- kernel 已经在目标内存位置

### 怎么测试

第一版只测：

- 打印 kernel 加载地址
- 打印 kernel 大小
- 跳转前打印 `kernel loaded`

---

## Step 8：建立页表并进入 long mode

### 你要做什么

从保护模式进入 64 位 long mode。

你至少要做：

1. 建四级页表
2. 开启 PAE
3. 设置 `EFER.LME`
4. 打开分页
5. 跳到 64 位代码段

### 为什么

因为你目标是 `x86_64` 内核，最终必须真正进入 long mode。

### 输入是什么

- kernel 已加载
- GDT 已就绪
- 页表已建立

### 输出是什么

- CPU 正在执行 64 位代码

### 怎么测试

测试点非常直接：

- 打印 `long mode ok`

如果 long mode 一进就死，优先怀疑：

- 页表映射错
- CR3 错
- EFER/CR0/CR4 顺序错
- 64 位代码段错

---

## Step 9：进入 kernel_main

### 你要做什么

在 64 位模式下：

1. 准备内核栈
2. 跳到 `kernel_main()`
3. 传入最小 boot info

### 为什么

因为从这里开始，才正式从“加载器逻辑”切到“内核逻辑”。

### 输入是什么

- 64 位 CPU 环境
- kernel 已在内存

### 输出是什么

- C++ 内核开始运行

### 怎么测试

最关键测试：

- 屏幕打印 `hello from os64 kernel`

只要这一步成功，你就已经不是停留在 bootloader 阶段了。

---

## Step 10：做最小 console

### 你要做什么

先做最简单屏幕输出。

最简单可以直接写 VGA 文本缓冲区。

### 为什么它优先级这么高

因为之后你要调：

- GDT
- IDT
- 内存映射
- 页表
- 调度器

没有输出几乎寸步难行。

### 输入是什么

- 一个字符串或数字

### 输出是什么

- 屏幕上可见内容

### 怎么测试

按顺序测：

1. 打印字符串
2. 打印换行
3. 打印十六进制地址
4. 打印十进制数字

---

## Step 11：做 IDT 和异常处理

### 你要做什么

进入内核后，先不要急着上完整中断控制器。

先把这几件事做好：

1. IDT
2. 默认异常入口
3. 至少一个异常处理函数
4. 打印异常编号

### 为什么

因为没有异常处理，后面一出错你只会看到黑屏。

### 输入是什么

- CPU 异常

### 输出是什么

- 屏幕能看到异常号

### 怎么测试

最简单的测试：

- 人为触发除零异常

通过标准：

- 不再静默死机
- 而是打印类似 `exception: divide by zero`

---

## Step 12：做最简单物理内存管理

### 你要做什么

第一版只做最简单页分配器。

你不需要一上来做：

- buddy
- slab
- 完整虚拟内存

第一版只要完成：

1. 存下内存映射
2. 找出 usable 区域
3. 逐页分配

### 为什么

这是后面页表、堆、进程、文件缓存的基础。

### 输入是什么

- 启动阶段拿到的内存映射

### 输出是什么

- `alloc_page()`
- `alloc_pages(n)`

### 怎么测试

第一版直接打印：

- 第一次分到的页地址
- 第二次分到的页地址
- 剩余页数量

通过标准：

- 地址递增合理
- 不会分到保留区

---

## Step 13：做调度骨架，不做完整调度

### 你要做什么

先定义最核心的数据结构：

1. task
2. pid
3. state
4. run queue

### 为什么先做骨架

因为真正的上下文切换还要依赖：

- 定时器中断
- 寄存器保存恢复
- 栈切换

第一阶段别一口吃完。

### 输入是什么

- 几个预先创建的任务对象

### 输出是什么

- 调度器状态能被打印出来

### 怎么测试

第一版只测：

- 创建 2 到 3 个任务
- 打印它们的状态

先别急着真正切换。

---

## 5. 第一阶段完整测试清单

你不要等“全写完”再测。

正确做法是每一层单独测。

### Test 1：Boot sector 存活测试

目标：

- 屏幕打印 `stage1 ok`

### Test 2：Stage 1 -> Stage 2 跳转测试

目标：

- 屏幕打印 `stage2 ok`

### Test 3：A20 测试

目标：

- 屏幕打印 `a20 ok`

### Test 4：保护模式测试

目标：

- 屏幕打印 `protected mode ok`

### Test 5：Long mode 测试

目标：

- 屏幕打印 `long mode ok`

### Test 6：Kernel 入口测试

目标：

- 屏幕打印 `hello from os64 kernel`

### Test 7：内存映射测试

目标：

- 打印 usable / reserved 区域信息

### Test 8：异常处理测试

目标：

- 手工触发除零
- 屏幕打印异常信息

### Test 9：页分配测试

目标：

- 连续分配几个页
- 地址和计数合理

### Test 10：调度骨架测试

目标：

- 调度器能打印当前任务表

---

## 6. 为什么我不建议你一开始就做这些

下面这些事都可以做，但不是现在：

- 完整可写文件系统
- ELF 完整加载器
- 用户态
- SMP
- 网络栈
- AI 策略系统

原因很简单：

> 你现在的主要矛盾不是“内核不够现代”，而是“内核还没被你亲手从 0 拉起来”。

---

## 7. 第一版目录结构建议

完全从零开始时，目录别拆太深。

第一版建议：

```text
os64/
├── README.md
├── Makefile
├── scripts/
├── boot/
│   ├── stage1.asm
│   └── stage2.asm
├── kernel/
│   ├── README.md
│   ├── boot/
│   ├── core/
│   ├── interrupts/
│   ├── memory/
│   └── runtime/
└── build/
```

现在这个项目已经按这套思路往前落地了，只是 `kernel/` 里面进一步按职责细分了。
如果你想直接看当前真实布局，可以打开：

- [kernel/README.md](./kernel/README.md)

---

## 8. 你现在最该做的第一件事

如果你要现在开工，顺序不要再摇摆，直接这样做：

1. 写 `scripts/check-env.sh`
2. 建 `boot/`、`kernel/`、`include/`
3. 写 `stage1.asm`
4. 先让它打印 `stage1 ok`
5. 再写 `stage2.asm`
6. 再做 long mode
7. 最后接 `kernel_main()`

---

## 9. 一句话总结

不用 `Limine` 的“从零构建”本质上不是先写内核功能，

而是先亲手打通这条链：

> `BIOS -> boot sector -> second stage -> long mode -> kernel`

只要这条链通了，你后面再加现代内核模块才有意义。

---

如果你要，我下一步就可以继续做两种选择里的一个：

1. 直接开始写第一个 `stage1.asm`，让它在 `QEMU` 里打印 `stage1 ok`
2. 先把 `Makefile + disk.img` 打包流程搭起来，给后面 stage1/stage2 留好位置
