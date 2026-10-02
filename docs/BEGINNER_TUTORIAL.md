# 从零开始：启动、使用并读懂当前的 os64

这本教程面向还没有学过操作系统的人。你不需要先理解页表或汇编；先启动系统、完成几个可观察的实验，再带着实验结果读代码。这里讲的是当前可交互版本。`docs/` 中带“第一版”“这一轮”的原教程保留了开发过程，已经加上历史阶段标记；它们适合补原理，不适合直接当最新安装说明。

你最终会完成四件事：让自己编译的系统启动，运行一个真正的用户程序，编辑并保存文件，解释一条命令怎样从键盘经过内核到磁盘。读到不懂的术语时，先看本章例子；没有必要第一次就记住所有寄存器和标志位。

喜欢先看图的读者可以并行阅读 [逐函数配图教程](illustrated/README.md)。每张图先解释一组机制，图下再给出函数名、输入与失败处理；它不会把长代码缩成看不清的小字。先用本章学会宿主和客体，再试 [多进程分块归约](illustrated/PARALLEL_REDUCTION.md)，能直接看到核心分配、分块结果和资源回收。

## 1. 先分清你正在操作哪台“电脑”

你的 macOS/Linux 是**宿主**。终端里的 `make build`、代码编辑器和 QEMU 都是宿主上的程序。QEMU 模拟另一台 x86_64 电脑；里面运行的 os64 是**客体**。os64 自己的 Shell 提示符是 `os64 % `。

| 你看到的地方 | 输入示例 | 谁来执行 |
| --- | --- | --- |
| macOS/Linux 终端 | `make build` | 宿主的构建工具 |
| QEMU 窗口或串口中 `os64 % ` 后 | `ls /bin` | os64 的内核 Shell |
| os64 编辑器自己的提示符 | 编辑器的 `help` / `save` | os64 的用户进程 |

`make` 不是 os64 的内建命令；os64 的 `/work/note.txt` 也不是宿主的同名文件。客体里的目录和文件被编码在宿主的 `build/data.img` 中。QEMU 为客体提供 CPU、RAM、键盘、屏幕和磁盘设备；内核在模拟 CPU 上直接执行自己的机器指令，不借宿主进程接口去假装操作系统。

本教程中标为 `bash` 的代码块在宿主终端执行；标为 `text` 且介绍为“在 os64 中输入”的代码块在客体输入。代码块不包含要一起输入的提示符。

## 2. 准备工具，完成第一次启动

### 2.1 工具分别做什么

只需要先理解这几个动作：

| 工具 | 任务 | 常见输出 |
| --- | --- | --- |
| NASM | 把汇编指令变成机器码 | `stage1.bin`、汇编对象文件 |
| Clang++ 或 x86_64 交叉 G++ | 把 C++ 变成目标 CPU 的对象文件 | `.o` |
| ELF 链接器 LLD 或交叉 LD | 把对象文件拼接并安排地址 | `kernel.elf`、用户 `.elf` |
| objcopy | 取出裸内核机器码/剥离调试信息 | `kernel.bin` |
| Python 3 | 生成文件系统和软盘布局 | `disk.img`、数据盘模板 |
| QEMU system x86_64 | 模拟整台电脑并启动镜像 | 客体窗口和串口输出 |

这里的 C++ 是 `freestanding`：不假定下面已经有操作系统或完整 C 标准库。内核不能像普通程序一样直接调用宿主的 `printf` 或 `fopen`。`clang++` 即使运行在 Apple Silicon 上，也可以按脚本的 `--target=x86_64-elf` 生成 x86_64 ELF 对象；这里用软件模拟，不要求宿主也是 x86_64。

### 2.2 安装

以下是常见安装方式；已安装时先运行检查，不必重复安装。Debian/Ubuntu：

```bash
sudo apt-get update
sudo apt-get install clang lld llvm nasm qemu-system-x86 python3 make
```

macOS 已有 Homebrew 时：

```bash
brew install llvm lld nasm qemu python
```

macOS 的系统链接器面向 Mach-O；本项目需要 ELF 链接器，不能只因为 `ld` 存在就认为工具齐了。脚本会优先搜索 PATH，并检查常见 Homebrew 安装位置，也接受 `NASM_BIN`、`CLANGXX_BIN`、`LD_BIN`、`OBJCOPY_BIN`、`PYTHON_BIN`、`QEMU_BIN` 覆盖。

如果还没有代码，在宿主终端下载并进入仓库：

```bash
git clone https://github.com/zmy1213/os64.git
cd os64
```

已有本地仓库就直接打开它，不用重复 clone。**仓库根目录**就是同时能看到
`Makefile`、`boot/`、`kernel/`、`user/`、`scripts/` 的目录；不一定与本文作者的
磁盘路径相同。宿主的 `pwd` 查看当前位置，`ls` 查看是否有 Makefile。

在**仓库根目录**执行：

```bash
make check-env
```

正常结果包含六项 `[PASS]`，最后有：

```text
[PASS] Ready to build and run x86_64 freestanding images.
```

这是“找到工具”的检查，不是完整兼容性认证。想记录版本，先从检查输出中取出实际工具路径，再在终端执行 `工具路径 --version`。例如 PATH 已配置时：

```bash
nasm -v
clang++ --version
ld.lld --version
llvm-objcopy --version
python3 --version
qemu-system-x86_64 --version
```

如果某个命令在 PATH 中找不到、但 `make check-env` 找到了 Homebrew 工具，使用检查输出中的那个路径。仓库没有声明“只能用某个精确版本”；能找到命令、成功构建以及回归通过是不同层次的验证。

### 2.3 构建与运行

```bash
make build
make run-gui
```

`make run-gui` 自带构建步骤；上面把两步拆开是为了方便第一次定位错误。窗口显示大量启动自测信息后，出现 `os64 % `。用鼠标点进 QEMU 窗口，然后输入 `help`。这是 BIOS 启动路线，不需要 GRUB、ISO 或安装到真实磁盘。

如果希望在当前终端使用串口：

```bash
make run
```

串口支持文字输入和基本行编辑。不要同时打开两个 QEMU 实例写同一份 `data.img`；遇到镜像写锁报错，先正常退出已经在运行的实例。

启动时可看到以下阶段消息，细节之间还会夹有测试日志：

```text
stage1 ok
stage2 ok
a20 ok
e820 ok
kernel loaded ok
boot volume loaded ok
protected mode ok
paging ok
long mode ok
```

正式交互前还会打印 `storage_backend=ata-pio`。这表示文件将保存到数据盘；若打印 `storage_backend=ram (volatile)`，当前使用内存回退卷，重启会丢失本次改动。不要只看能输入命令就推断磁盘挂载成功。

### 2.4 第一次运行的练习

在 os64 中逐行输入：

```text
help
pwd
ls /bin
run /bin/hello Alice
run /bin/echo "hello os64"
ps
mem
```

`hello` 的关键输出应包含：

```text
hello from os64 userland
argc=2
argv[0]=/bin/hello
argv[1]=Alice
run_exit_code=0
```

Shell 还会打印 `run_pid`、`run_entry`、装载页数等信息。PID、空闲页数、入口的精确低位可能随版本改变；不要拿旧教程的具体数值当成功标准。`run_exit_code=0` 表示这个程序按自己的约定成功结束。`"hello os64"` 带引号，会作为一个参数；引号不是参数内容。

**检查自己理解了没有：**宿主终端里的 `ls` 列出了哪个目录？QEMU 中的 `ls /bin` 列出了哪个目录？如果你能区分它们，接下来的保存操作就不容易做错。

## 3. 文件保存、备份与更新：先保护学习成果

### 3.1 三份镜像不要混淆

| 文件 | 内容 | 是否应该保存个人文件 |
| --- | --- | --- |
| `build/disk.img` | 自写启动器、内核、RAM 自测卷 | 否，可重新构建 |
| `build/data_volume.bin` | 本次构建的干净数据盘模板 | 否，构建会重生成 |
| `build/data.img` | QEMU 使用的持久数据盘 | 是 |

普通 `make build` 与 `make clean` 保留 `data.img`；clean 也保留工具更新生成的 `data.img.backup-*`。已有数据盘中的 `/bin` 程序也会保留，因此重新编译用户程序不会自动覆盖盘里的旧版。正常更新使用 `make update-tools`，不要直接重置数据盘。`make reset-data` 会把数据盘替换成模板，删除客体里的个人文件；`make distclean` 会删除整个 `build/`，也包括数据盘。不要把这两个命令当日常更新步骤。

### 3.2 已有数据盘，安全安装新版工具

第一次构建已经会创建含新工具的数据盘；从较早 checkout 更新时，先在 os64 输入
`shutdown`，确认 QEMU 退出，然后在宿主执行：

```bash
make update-tools
```

脚本在临时副本中复用真实 OS64FS 更新 `/bin`，校验工具、重新挂载，并对比所有
非 `/bin` 文件的内容、inode 和 mode；成功后才替换镜像。同时生成完整的
`build/data.img.backup-UTC时间戳` 备份。磁盘在用或空间不足时拒绝替换，原盘保留。
当前此目标使用默认 `build/data.img` 与 `build/user/`；它不是运行中的客体命令。
更新后 `make run-gui`，`ls /bin` 应能看到 edit、mem_test、stackfault、nxfault。

### 3.3 完成一次持久化实验

在 os64 中输入：

```text
mkdir /work
write /work/note.txt hello
append /work/note.txt _os64
cat /work/note.txt
sync
reboot
```

`cat` 应显示 `hello_os64`；`sync` 应显示 `sync ok`。重启后再次输入：

```text
cat /work/note.txt
```

内容仍是 `hello_os64`，才完成“跨重启保存”的实验。如果 `/work` 已经存在，`mkdir` 可以报已存在导致的失败；继续用已有目录即可。路径从 `/` 开始叫绝对路径；`cd /work` 后的 `cat note.txt` 则是相对当前目录解析的路径。

成功返回的文件变更本身已经写回并 flush；`sync` 是可观察的额外同步操作，不是唯一触发持久化的地方。`reboot`、`shutdown` 会先同步，失败时取消电源操作。关闭前优先在 os64 输入 `shutdown`。

这仍没有断电日志：提交到一半时强制终止 QEMU，可能留下不一致的数据卷。运行时 I/O 失败可回滚，与突然断电能恢复，是两件不同的能力。详细边界见 [持久化存储](./PERSISTENT_STORAGE.md)。

### 3.4 在宿主备份

先在 os64 中 `shutdown`，确认 QEMU 已退出，再在仓库根目录执行：

```bash
mkdir -p backups
cp -p build/data.img "backups/data-$(date +%Y%m%d-%H%M%S).img"
```

备份的对象是整份数据镜像。不要在 QEMU 正在写入时复制它并认为得到了完整一致的快照。恢复时也先关闭 QEMU，再把你明确选择的备份复制到 `build/data.img`；这会替换当前客体文件，先保留当前镜像副本。

如果想体验一份全新模板，又不动原数据盘，可使用另一个构建目录：

```bash
BUILD_DIR="$PWD/build-tutorial" make build
BUILD_DIR="$PWD/build-tutorial" make run-gui
```

后续继续运行这份实验盘时，每次都带同一个 `BUILD_DIR`。省略它会回到默认 `build/`。这能让新手做空盘实验，也能避免为了看到新工具而误删旧笔记。

## 4. 读代码前的最少基础知识

### 4.1 位、字节和十六进制

一个位只有 0 或 1。8 个位组成一个字节，可以表示 0–255。`0x` 开头的数字是十六进制，数字除了 0–9，还使用 A–F 表示 10–15；每一个十六进制位刚好表示 4 个二进制位。

| 写法 | 十进制值 | 在这里的含义 |
| --- | --- | --- |
| `0x10` | 16 | 不是十进制 10 |
| `0x200` | 512 | 一个磁盘扇区/文件系统块的字节数 |
| `0x1000` | 4096 | 一个普通内存页的字节数 |
| `0x100000` | 1,048,576 | 1 MiB |
| `0x400000` | 4,194,304 | 当前用户窗口的起点 |

1 KiB = 1024 字节，1 MiB = 1024 KiB。一个 4096 字节页包含 8 个 512 字节扇区，但内存页和磁盘扇区是不同层次的单位，不是同一种资源。

地址是“从哪里找”的编号；内容是“在那里读到了什么”。`0x400000` 可以是地址；那个地址里的字节可能恰好是 `0x48`。在 C++ 中，指针保存地址，`*pointer` 才去访问内容。

### 4.2 寄存器和栈

寄存器是 CPU 内部少量、快速的存储位置。汇编里的 `mov ax, 0` 是把 0 放进 AX；`mov [address], ax` 则把 AX 内容写到内存。方括号提示“访问这个地址里的内容”。

| 名字 | 先记住什么 |
| --- | --- |
| AX / EAX / RAX | 同一寄存器的 16/32/64 位视图；AL/AH 是它的低字节/次低字节 |
| RIP | CPU 接下来执行代码的位置 |
| SP / ESP / RSP | 不同模式下的栈顶位置 |
| RFLAGS | CPU 的状态标志；IF 控制可屏蔽中断 |
| CR3 | 当前页表根的物理地址 |

函数 `call` 要记住回来继续执行的位置；局部变量也常放在栈上。x86 栈通常向较小地址增长：栈顶从 `0x800000` 往下移动。因此“给栈留空间”是在栈顶下方映射内存，不是在上方。

`uint64_t` 表示一个占 8 字节的无符号整数；`sizeof` 返回对象占多少字节。源码中的 `constexpr` 常量在编译时确定；写 `0x1000` 不是动态申请内存。

### 4.3 物理地址、虚拟地址和页权限

物理地址描述机器的内存位置。虚拟地址是程序看到的位置；CPU 根据**当前页表**把虚拟地址翻译成物理地址。例如两个进程都访问虚拟地址 `0x400000`，它们使用不同 CR3，所以可以指向不同物理页。地址数字相同并不意味着共享了内容。

一个页表项既有物理页编号，也有权限：

| 权限 | 作用 | 错误理解 |
| --- | --- | --- |
| present | 映射存在 | 不存在的页不能靠指针直接访问 |
| writable | 可以写 | “能读”不代表“能写” |
| user | ring 3 可以访问 | 高地址映射也不能自动给用户访问 |
| NX | 不允许取指令执行 | 数据页里的字节不应随便作为代码执行 |

CPU 违反权限或访问未映射页会产生 page fault（页错误）。本系统把用户页错误当作该程序的失败，结束它并恢复 Shell；内核自己的页错误仍会诊断并停机。当前没有“缺页后自动把数据从磁盘搬进来”的 demand paging。

### 4.4 内核、用户程序、系统调用

内核运行在 ring 0，能管理页表和设备。普通程序运行在 ring 3，权限受限。系统调用是程序请求内核服务的受控入口，例如“把这些字符写到屏幕”或“打开一个文件”。

正常交互的 `cat` 会在 `/bin` 找到用户 ELF，切入 ring 3 后通过 syscall 读文件；`run /bin/cat` 执行同一个程序并额外打印装载/退出诊断。启动时 RAM fixture 的旧 Shell 自测仍保留内核内建 `cat`，方便对照历史里程碑；不能把那段自测输出当作现在交互命令的执行位置。

### 4.5 看 C++ 时的语法路标

不必先学完整 C++，先认出这些写法就能读接口：

| 写法 | 小例子 | 这里怎样理解 |
| --- | --- | --- |
| 声明/实现 | `.hpp` 的函数名与 `.cpp` 的函数体 | 接口约定与实际动作分开 |
| struct | `struct Buffer { ... };` | 把相关数据放进一个对象 |
| 指针 `*` | `Buffer* p`、`*p` | 前者声明地址变量，后者访问所指对象 |
| `.` / `->` | `buffer.size`、`p->size` | 前者取对象成员，后者经指针取成员 |
| nullptr | `if (p == nullptr)` | 空指针，没有合法对象 |
| bool / return | `return false;` | 返回成功/失败；退出当前函数 |
| `&` | `Buffer& b`、`&buffer` | 前者是引用（同一对象别名），后者取地址；要看出现位置 |
| reinterpret_cast | `reinterpret_cast<char*>(address)` | 重新解释地址类型，不创建映射、不申请内存、不保证地址合法 |

例如 `auto* bytes = reinterpret_cast<char*>(0x400000);` 只是得到一个地址值；
只有当前页表真的允许访问，`bytes[0]` 才能读写。`const char*` 表示通过这个指针
不能修改字符；`volatile` 要求访问确实发生，不要与页表的可写/NX 权限混为一谈。

## 5. 构建与启动：从文件到第一条内核指令

### 5.1 `make build` 做了什么

先打开 [构建脚本](../scripts/build-stage1-image.sh)，只追“输入是什么、输出是什么”：

1. 编译 `kernel/**/*.cpp`，汇编 `kernel/**/*.asm`，得到对象文件。
2. 用 [内核链接脚本](../kernel/boot/linker.ld) 把内核链接为 `kernel.elf`。内核入口固定在低地址区域，当前从 `0x10000` 起。
3. objcopy 生成 `kernel.bin`。**stage2 装载的是这个裸文件，不是运行时解析 kernel.elf。**
4. [用户构建脚本](../scripts/build-user.sh) 编译 `user/programs/*.cpp`，生成用户 ELF。
5. [卷生成脚本](../scripts/make-volume.py) 生成 RAM 自测卷和数据盘模板。用户 ELF 成为模板中的 `/bin/...` 文件。
6. [镜像布局脚本](../scripts/make-boot-image.py) 计算内核扇区数，生成 `kernel_meta.inc`，汇编 stage1/stage2，再组装软盘镜像。

不要手改生成的 `kernel_meta.inc`；内核变大时扇区数会变化。脚本检查内核文件段不越过物理 `0x80000` 的启动卷；BSS 是无文件字节的另一段，固定放在物理 `0x100000`–`0x160000` 的保留窗口，entry64 在调用 C++ 前显式清零。只增大读盘计数不能解决 RAM 布局冲突。

### 5.2 BIOS 与 stage1

QEMU 的 BIOS 从启动软盘读取第一个 512 字节扇区到物理 `0x7c00`，然后执行它。末尾的字节 `55 AA` 是启动签名，表示这个扇区具有 BIOS 所期待的标记；签名本身不是程序指令。

打开 [boot/stage1.asm](../boot/stage1.asm)，看 `start` 和 `load_stage2`：

- `cli` 先关闭中断，设置 DS/ES/SS 和栈。CPU 不能在栈还没准备好时被打断。
- 保存 BIOS 放在 DL 中的启动盘号，后面读盘继续用它。
- 打印 `stage1 ok`，这是最早的检查点。
- `load_stage2` 用 BIOS 的 `int 0x13` 从第 2 个扇区读 8 个扇区到 `0x8000`。
- `jmp 0x0000:STAGE2_OFFSET` 把执行权交给 stage2。

这里的 `int 0x13` 是 BIOS 在实模式提供的服务，与后面的用户 `int 0x80` 不是一套接口。stage1 空间太小，所以只做启动和接力。

### 5.3 stage2 建好 64 位环境

打开 [boot/stage2.asm](../boot/stage2.asm)，按 `start` 中的调用顺序读：

| 步骤/函数 | 在解决什么问题 | 能观察什么 |
| --- | --- | --- |
| `enable_a20` | 避免早期兼容机制让地址绕回低内存 | `a20 ok` |
| `collect_e820` | 向 BIOS 请求哪些物理区间是可用 RAM | `e820 ok`，之后的 E820 列表 |
| `load_kernel_from_disk` | 按生成布局把 kernel.bin 读到内存 | `kernel loaded ok` |
| `load_boot_volume_from_disk` | 预读固定的 RAM 自测卷 | `boot volume loaded ok` |
| `protected_mode_start` | 换到 32 位保护模式并设置段/栈 | `protected mode ok` |
| `setup_page_tables` | 建最初低 2 MiB 恒等映射 | `paging ok` |
| `enable_long_mode` / `long_mode_start` | 启用分页和 long mode，执行 64 位代码 | `long mode ok` |

“恒等映射”意思是这一阶段虚拟地址和物理地址数值相同。它够用来启动低地址内核；它不是当前内核最终只能使用低 2 MiB 的原因。后续内核会建立更大的直接映射。

GDT 可以先理解为“描述 CPU 代码/数据段及权限的表”；这里配合模式切换。页表则负责虚拟→物理翻译，它们是不同的表。无需第一次读就手算每个段描述符。

stage2 最后把 `BootInfo` 地址放进 RDI，并跳到内核入口。`BootInfo` 是双方约定的参数结构，包含 E820 数组、启动卷位置、扇区大小等；C++ 入口不能凭空知道这些信息。

### 5.4 entry64 和 kernel_main

[kernel/boot/entry64.asm](../kernel/boot/entry64.asm) 的 `kernel_entry` 把 RSP 设置为 `0x180000`，清零 RBP，再调用 `kernel_main`，沿用 RDI 中的 BootInfo。栈顶低于早期 2 MiB 恒等映射上界，所以刚进内核时可用；分配器还必须单独保留这根栈，不能发给用户页。

[kernel/core/kernel_main.cpp](../kernel/core/kernel_main.cpp) 是初始化和自测总装配处：检查启动参数，初始化内存与异常处理，建立文件接口、时钟、调度、键盘等，再进入正式交互。它很长，因为保留了许多逐阶段 smoke test；第一次不要从头读完，搜索你正在研究的函数名。

正式启动先在 RAM fixture 上做回归，然后探测 ATA 数据盘、挂载正式文件系统、安装 syscall 服务，最后 `start_kernel_shell_under_scheduler` 创建 Shell 线程。自测卷里的旧文字（例如“read-only”）是固定回归内容；已有数据盘的 /readme.txt 也可能保留旧文字，因为 update-tools 故意不改非 /bin 文件。不能根据这些文本判断当前存储仍然只读，应看 storage_backend 日志与实际写入/重启实验。

**练习：**分别找到 `stage1 ok` 和 `long mode ok` 的字符串，再找使用它们的位置。说明“打印这一行”之前完成了哪些动作。不要修改启动地址做实验；先建立可回溯的检查点。

## 6. 当前内存设计：从“能启动”到“能跑更大程序”

### 6.1 当前地址布局

以下范围的上界都不包含在范围内，例如 `[4 MiB, 8 MiB)` 的最后一个字节是 `8 MiB - 1`。

| 地址范围 | 含义 | 用户能否访问 |
| --- | --- | --- |
| 低 0–2 MiB | 启动恒等映射，低地址内核与早期数据 | supervisor 页面不可由 ring 3 访问 |
| 物理 `0x100000`–`0x160000`（低地址恒等映射） | 内核 BSS 保留窗口，entry64 显式清零实际 BSS | supervisor |
| 物理 `0x170000`–`0x180000`（低地址恒等映射） | 保留的 64 KiB bootstrap 内核栈，栈顶 `0x180000` | supervisor |
| `0x400000`–`0x800000` | 每个进程自己的 4 MiB 用户窗口 | 按映射与权限决定 |
| ELF 结束后页边界–当前 break | 用户堆 | 已申请的页可读写，NX |
| `0x7ef000`–`0x7f0000` | 用户栈下方 4 KiB guard | 不映射 |
| `0x7f0000`–`0x800000` | 64 KiB 用户栈，16 页 | 可读写，NX |
| `0x1000000`–`0x1400000` | 16–20 MiB 的 4 MiB 内核堆 | supervisor |
| `0xffff800000000000` 起 | 内核对低 256 MiB 物理区间的直接映射 | supervisor |

用户窗口**不是**“程序自动拥有 4 MiB RAM”：只有实际映射的段、栈和申请的堆页才有物理页支撑。栈不自动继续增长；越过 guard 会终止程序。

### 6.2 物理页分配器与直接映射

旧实现把页表和用户数据都限制在 1–2 MiB 物理池，因为内核把物理地址直接当指针使用，只能访问早期已恒等映射的区域。新的解决方式不是假定高地址 RAM 已经能读写，而是给它一个内核虚拟地址。

直接映射的关系是：

```text
内核虚拟地址 = 0xffff800000000000 + 物理地址
```

例如物理 `0x300000` 对应直接映射虚拟 `0xffff800000300000`。这是同一份物理内存的另一个访问地址，不是复制了一份 RAM。只给内核访问；建立映射也不意味着 BIOS 标为保留的设备区间可以拿来分配。

读 [page_allocator.*](../kernel/memory/page_allocator.cpp) 时，先找初始化、`alloc_page`、`free_page`、`count_free_pages`。每页 4 KiB；管理范围最高 256 MiB，并继续尊重 BIOS E820 的可用区间。实际默认 QEMU RAM 是 128 MiB，因此“上限 256 MiB”不代表它真的有这么多可分配 RAM。

低 1 MiB 保留给启动器、内核文件、启动卷及设备布局；内核 BSS 另外保留物理 `0x100000`–`0x160000`，bootstrap 栈另保留 `0x170000`–`0x180000`。E820 的 usable 只说明这是 RAM，不能说明这些内核状态已经可以分配给用户。忽略 BSS/栈保留会让用户堆覆盖全局状态或启动现场。

分配器用总计 16 KiB 的两张位图记录哪些页可供分配、哪些已经分配。位图是把许多“是/否”压进字节中的表：第 N 个位对应第 N 页。分配时选一个可用且未分配的页并标记；释放时检查这个页是否属于管理范围且真的被分配，拒绝重复释放。它不再只回收旧低地址池。

[paging.*](../kernel/memory/paging.cpp) 负责建立直接映射与页表访问。页表项中的 CR3/物理页编号依然是物理地址；只有 C++ 要读写该页内容时，才通过物理→内核指针转换。把两种地址混用，是内核开发里非常常见的故障源。

### 6.3 为什么还要内核堆

如果只给 `alloc_page` 传“给我 1 页”，它只会分配 4096 字节的整页。文件句柄或线程对象通常只需要几十/几百字节；每个都占一页很浪费。

[heap.cpp](../kernel/memory/heap.cpp) 的 `heap_alloc` 在已映射堆中切小块，`heap_free` 把小块放回空闲链并合并相邻空闲区；[kmemory.*](../kernel/memory/kmemory.cpp) 提供 `kmalloc/kfree/knew/kdelete`。当前内核堆仍固定 4 MiB，预映射后在进程中共享，释放对象是让堆块可复用，不代表把整个堆的物理容量归还系统。

“内核堆”和“用户堆”拥有不同地址、权限、分配器。用户程序不能调用内核 `kmalloc` 获取 ring 0 指针。

### 6.4 用户 `brk` 与小块分配库

系统调用 20 `brk` 管理当前进程的堆末尾地址。heap base 是 ELF 各段结束位置中最大者向上对齐到页边界；heap limit 是 `0x7ef000`，防止碰到栈 guard。

```text
ELF 段 | 用户堆从这里向高地址增长 → | 空闲间隔 | guard | ← 栈向低地址使用
```

`brk(0)` 查询当前末尾；`brk(new_end)` 请求新末尾。它返回**实际末尾地址**，失败也返回旧末尾，所以必须比较结果与请求，不能只判断是否非零。扩张时马上分配并清零新页，缩小时释放不再需要的整页并清理保留页的尾部。中途分配失败会撤销本次新映射，保持旧 break。

例子，以下是用户程序里的 C++，不是 Shell 命令：

```cpp
uintptr_t old_end = brk();
uintptr_t wanted = old_end + 4096;
if (brk(wanted) != wanted) {
  error("no user heap space\n");
  return 1;
}
auto* bytes = reinterpret_cast<char*>(old_end);
bytes[0] = 'A';
brk(old_end); // 本例确认没有其他堆对象后，才把这页归还。
```

日常程序使用 [memory.hpp](../user/memory.hpp) 的 `memory::allocate/release/resize` 管理小块；不要在库还有活对象时手动移动 break，否则库保存的指针可能失效。release 会合并空块，只有末尾空块才可通过 brk 缩小；中间空块留待复用。这是小型教学分配器，没有提供完整 libc 的 malloc/free 接口。

### 6.5 ELF、NX 和栈保护

[load_elf_user_program](../kernel/task/elf_loader.cpp) 接受 little-endian x86_64 ET_EXEC，最多 8 个程序头、256 张 PT_LOAD 页，整个文件最多 69,632 字节（与当前文件系统单文件容量相同）。文件大小和运行内存大小不同：BSS 在 ELF 中只记录运行时长度，装载时分配并清零，所以文件很小也可能需要很多内存页。

当前装载流程是：

1. 打开文件，读取到可释放的内核堆暂存缓冲。
2. 检查 ELF 头和所有可装载段：偏移/长度无溢出，位于用户范围，页不重叠，入口属于可执行段。
3. 拒绝同时可写可执行的段，拒绝段覆盖栈 guard/栈区域。
4. 逐页分配并清零物理页，建立带实际权限的用户映射，再通过内核直接映射逐页复制文件字节；未被文件内容覆盖的 BSS 保持为零。
5. 建立用户栈与初始参数，成功后让调度器启动；任何失败路径撤销已创建的资源。

支持 NX 的 CPU 上，代码段可执行且不可写，数据、堆、栈可写且 NX。内核先通过 CPUID 检查，再打开 EFER.NXE；日志 `nx_enabled=1` 才代表这项硬件保护已生效。不支持 NX 时 `nx_enabled=0`，内核不设置非法 NX 位。NX 是“不能执行”，它不防止所有程序错误，但让误把数据当指令执行更容易被拒绝。

源码里声明一个可写数组，不保证优化后仍落在可写 ELF 段；从未修改的内容可能被当成只读常量。`nxfault` 在运行时写入 volatile 数组，测试还检查最终 ELF 段归属，保证它确实在尝试执行数据。

guard 不是“能捕获一切栈溢出”的魔法：它是栈下方故意留空的一页，正常向下越界首先触碰它时会出错；大幅跳过 guard 的错误访问仍需靠完整地址布局与程序检查限制。用户栈有 guard，不意味着当前每根内核栈都有硬件 guard。

**练习：**在 os64 中运行 `mem`，运行 `run /bin/mem_test`，再运行 `mem`。程序应输出 `mem_test large_elf_ok`、`mem_test large_stack_ok`、`mem_test brk_zero_reject_ok`、`mem_test heap_1m_reuse_ok`，并以 0 退出。比较的是运行后资源是否回收；不能把“内存检查一次通过”解释成无限程序规模或按需分页已经实现。

## 7. `run /bin/hello` 的进程旅程

### 7.1 进程与线程为什么分开

进程是“资源的主人”：自己的页表、用户内存、文件描述符表、当前目录、退出状态。线程是“CPU 正在执行的那条路线”：寄存器、栈、运行/等待状态。当前一般用户进程只有一条主线程；内核调度结构可以容纳更多线程，但没有通用用户线程创建 API。

当前固定最多 16 个 PCB（进程记录）、32 个 TCB（线程记录），Shell/idle 等也会用资源。不能把这理解成能无限次同时 spawn；反复顺序运行之所以可行，是因为退出后会回收槽位。

### 7.2 顺着源码追一次启动

在 [shell.cpp](../kernel/shell/shell.cpp) 找 `handle_run_command`，在 [scheduler.cpp](../kernel/task/scheduler.cpp) 找 `scheduler_create_user_elf_thread`：

```text
读取整行 → 解析 run 和参数 → 按 cwd 解析路径
→ 创建 PCB / 私有页表 / fd 与 syscall 上下文
→ 装载 ELF / 用户栈 → 写 argc/argv
→ 创建 TCB 并放进 ready queue
→ Shell 阻塞等待 PID → 用户线程运行
→ exit 或用户异常 → 唤醒 Shell → 打印退出码 → 回收
```

`scheduler_prepare_user_arguments` 把字符串与指针写入新进程的栈。入口 [user/start.asm](../user/start.asm) 从初始 RSP 读取 `argc`，让 `argv` 指向之后的指针表，再调用 `main(argc, argv)`。最多 8 个参数（包括 argv[0]），每个最多 63 字节；栈变大不自动改变这个接口限制。

每个用户线程另有两根 32 KiB 的 supervisor 内核栈：一根保存启动/返回调度器的现场，一根接收 ring 3 的 syscall、IRQ、异常。TSS 的 `rsp0` 指向进入栈；共用一根栈可能让后来的中断覆盖最初的返回现场。

### 7.3 抢占、睡眠与等待

当前可用 1–4 个虚拟 CPU，各自的时钟 IRQ 会周期性打断当前用户程序。BSP 的 PIT 管全局时间，AP 的本地 APIC 时钟管本核时间片。调度器记录时间片并可切去其他线程，原用户寄存器现场留在中断帧中，恢复后继续执行原位置。这叫用户态抢占。

`sleep` 是定时阻塞；`waitpid` 等子进程退出；读 stdin 没有字符时等待键盘事件。等待时不需要一直做空循环，否则浪费 CPU，甚至让需要执行的其他线程无法推进。内核内部仍依赖明确的 yield/sleep/block 点，不是全抢占内核。

在 os64 中运行：

```text
run /bin/spawn_test
run /bin/sleep 100
run /bin/spin 10000000
```

第一项应看到 `spawn child ok` 和 `spawn_test child_status=0`。这里是用户父进程通过系统调用创建用户子进程。spawn 继承 cwd 和打开的 fd 引用；父子可以通过继承的管道交换数据，普通文件偏移也共享。waitpid 只允许等待自己的子进程。

### 7.4 退出为何不留下永久垃圾

最后一条线程退出时进程进入 exited。`scheduler_wait_process` 负责等和取状态；退出时立即关闭 fd 与 UDP 端点，避免等待回收前阻塞其他进程；`scheduler_reap_process` 释放用户页/私有页表/内核栈并清理 PCB/TCB。waitpid 把等待和回收串起来；Shell 的 run 则显式做这两步。

父进程先退出时，其子进程会被标记自动回收。调度器在另一根栈上回收，不能释放当前正在执行的栈再继续用它。更多细节见 [进程运行时](./PROCESS_RUNTIME.md)。

## 8. 一次 `print` 怎样进入内核

打开 [user/os64.hpp](../user/os64.hpp)，找到 `print`、`write`、`syscall`：

```text
print("hello")
→ write(1, 字符串地址, 5)
→ RAX=9, RDI=1, RSI=字符串地址, RDX=5
→ int 0x80 → 内核检查与分发 → 结果放回 RAX → 返回用户程序
```

RAX 的 9 是本项目“write”的编号；fd 1 是 stdout。相同 `int 0x80` 指令在 Linux 上不等于这套 ABI。ABI 是双方约定的编号、参数位置、结构布局和返回值。这里最多五个参数使用 RDI/RSI/RDX/RCX/R8，UDP send 用到了第五个。

在 [interrupt_stubs.asm](../kernel/interrupts/interrupt_stubs.asm) 看寄存器保存，在 [syscall.cpp](../kernel/syscall/syscall.cpp) 找 `kernel_handle_syscall`、`user_syscall_arguments_valid`、`dispatch_syscall_registers`。关键不是把调用号转成函数名，而是进入内核后仍不能相信用户传来的地址。

`write` 从用户缓冲读数据，要求该范围已映射且用户可读；`read` 要向用户缓冲写数据，必须额外可写。检查覆盖**每一页**：一个起点合法的缓冲区也可能跨入未映射页。路径还必须在长度上限内有 NUL 结束符。

在 os64 中输入：

```text
run /bin/badptr
run /bin/fault
run /bin/ud2
run /bin/hello still_alive
```

`badptr` 应显示坏指针/路径/标志被拒绝。`fault` 页错误约定退出码为 142（128 + 向量 14），`ud2` 非法指令为 134（128 + 向量 6）；这些演示程序故意失败，后面的 hello 仍应正常运行。用户失败隔离不代表内核 bug 能自动修复。

## 9. 从文件路径到磁盘扇区

### 9.1 路径和文件描述符

路径如 `/work/note.txt` 是给用户看的名字。文件系统用 inode 编号标识对象，并在目录里保存“名字→inode”的对应关系。打开文件后，进程得到一个小整数 fd；读写 fd 时句柄记住当前位置，不必每次重新从路径寻找。

这里 fd 0/1/2 分别是 stdin/stdout/stderr，普通文件从 3 开始。每个进程有 16 个普通文件表槽；公开 fd 加了标准流偏移，不能把底层槽号当成用户 fd。

`/bin/cat` 的核心调用路径：

```text
用户 open/read/close
→ syscall 与当前进程的 fd 表
→ VFS 统一接口
→ FileHandle / OS64FS
→ BlockDevice
→ ATA PIO，或 RAM 回退卷
```

[fd.cpp](../kernel/fs/fd.cpp) 管小整数和句柄；[file.cpp](../kernel/fs/file.cpp) 管文件位置；[vfs.cpp](../kernel/fs/vfs.cpp) 给上层提供统一入口；[os64fs.cpp](../kernel/fs/os64fs.cpp) 理解实际磁盘格式。它们是层层分工，不是同一份功能重复写四遍。

### 9.2 当前 OS64FS v3 格式

| 区域 | 把它理解为 |
| --- | --- |
| superblock | 卷的说明书：签名、容量、各区位置、计数 |
| inode bitmap | 哪些 inode 槽已使用 |
| data bitmap | 哪些数据块已使用 |
| inode table | 每个文件/目录的类型、长度和块编号 |
| data area | 文件内容、目录项和间接块表 |

每块 512 字节。每文件有 8 个直接块编号，之后用一个间接块保存最多 128 个编号，所以最大 `(8 + 128) × 512 = 69,632` 字节。inode 里保存了 `mode` 字段，但当前没有用户/组身份与权限实施；看见字段不等于功能已经完整。

默认数据盘有 64 个 inode 槽，0 号保留；格式最大 128 槽。文件名最多 56 字节，格式路径最多 16 层；syscall 路径字符串还受 63 字节上限约束，这是更早碰到的接口限制。`rm` 只删普通文件与空目录，仍打开的文件和活动 cwd 不能删除。

### 9.3 挂载和写入为什么要检查

`initialize_os64fs` 读布局和缓存，再核对 inode 引用、目录引用、位图与统计。坏卷不会被自动格式化覆盖；正式启动可能回退 RAM，并打印挂载错误。

`mutate` 是变更事务入口。它先在堆里暂存原扇区、新扇区和缓存快照；准备阶段只改暂存内容。操作与一致性检查通过后，才写设备并 flush。提交 I/O 失败时尝试恢复包括失败尝试在内的原扇区；恢复仍失败则取消挂载，避免继续破坏卷。

这可以保护“运行时能发现并处理的失败”，但暂存内容在 RAM；断电后 RAM 消失，不能当磁盘日志恢复。当前每次事务最多记录 256 个修改扇区。

### 9.4 ATA 是实际设备接口

[ata_pio.cpp](../kernel/storage/ata_pio.cpp) 的 `initialize_ata_pio_primary_master` 发 IDENTIFY；`ata_pio_read_sector` / `ata_pio_write_sector` 用端口传输单扇区；`ata_pio_flush` 发送 FLUSH CACHE。状态等待是有上限的轮询，因此设备错误不会无限等下去。

ATA 数据盘由 QEMU `if=ide,index=0` 挂到 primary master。它是独立于启动软盘的第二个镜像。当前是 LBA28、512 字节逻辑扇区；没有 AHCI/NVMe，也没有真实硬件适配认证。

**练习：**运行 `run /bin/fs_test /work/large.txt` 再 `stat /work/large.txt`，观察间接块信息；重启后运行 `run /bin/fs_test /work/large.txt verify`。这项测试使用多个块，能观察越过 8 个直接块后的读写；不要覆盖你已有的同名文件。

## 10. 在客体内使用行式文本编辑器

`/bin/edit` 是 ring 3 用户程序，通过用户堆持有文本，通过 stdin 逐行接收命令，再通过系统调用保存。它不是全屏图形编辑器；可以把它理解为“显示带行号的文本，每条命令改一行”。

先在 os64 输入：

```text
run /bin/edit /work/lesson.txt
```

进入编辑器后提示符是 `edit> `。在一个全新文件中逐行输入：

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

第一次 print 应显示三行；delete 后应是 `1: second line` 和 `2: third line`。
save 应显示 `edit: saved 23 bytes`。退出回到 Shell 后输入：

```text
cat /work/lesson.txt
reboot
```

重启后再 `cat /work/lesson.txt`，应仍显示 second line 和 third line。已有同名文件时
上面的行号和字节数会不同，请选新实验路径；不要为了匹配例子覆盖个人笔记。

命令有 `print`、`append TEXT`、`insert N TEXT`、`delete N`、`save`、`quit`、`quit!`、
`help`。行号从 1 开始，insert 的 N=当前行数+1 时追加。没有替换整行命令，可先删再插。
append/insert 会添加换行；已有无末尾换行文本载入时保留原样，末尾追加时补分隔换行。
TEXT 不做 Shell 引号解析，输入的引号会成为文件内容。

改动在内存中；`quit` 遇到未保存修改会提醒，`quit!` 明确丢弃它们。文件最多
32,768 字节，命令最多 512 字节，含 NUL 的二进制文件/超限文件/读失败会被拒绝，
不会把只读到的前半部分当完整文件保存。完整接口见 [用户程序说明](../user/README.md)。

保存使用 syscall 21 `replace_file(path, buffer, size)` 把“创建/替换整个文件”作为一次 OS64FS 事务，返回写入字节数或负错误码。它避免先 `open(TRUNC)` 已把原文清空、随后写失败导致原文丢失的两阶段问题。保存失败时需要继续保留编辑缓冲，修正空间/设备问题后再试；运行时事务保证仍不等于断电原子性。

源码学习顺序是 [edit.cpp](../user/programs/edit.cpp) 的输入/命令解析与缓冲管理，再看用户库分配器和 `replace_file` 包装，最后看内核 `sys_replace_file` 到 `vfs_write_file` / `mutate`。你会看到此前章节的内存、用户进程、系统调用、文件事务一起被实际应用使用。

## 11. 如何验证，如何定位失败

### 11.1 按规模选择检查

```bash
make test
```

完整回归包括启动/内存/用户切换原 smoke、正式系统交互、冷启动持久化、宿主 ASan/UBSan 存储与物理页故障测试、真实 QEMU 用户堆/保护/编辑器回归，以及内核异常诊断。各项也可独立执行：

| 命令 | 主要目的 |
| --- | --- |
| `make test-stage1` | 最底层启动、trap、调度与原里程碑 |
| `make test-system` | 真实 QEMU 输入、正式用户程序/回收/保存流程 |
| `make test-storage-host` | 空间不足、损坏元数据、I/O/flush 失败回滚 |
| `make test-memory-host` | E820/位图、重叠区/保留栈、分配回收与 double free |
| `make test-memory-user` | 大 ELF、1 MiB 堆、缩堆清零、栈/NX、编辑与失败/冷启动保存 |
| `make test-page-fault` | 故意触发内核页错误，检查诊断路径 |
| `make test-invalid-opcode` | 故意触发内核非法指令，检查诊断路径 |
| `make test-shell-host`、`make test-ipc` | 引号/语法边界、真实多进程管道、重定向、EOF/回收 |
| `make test-syscall-boundaries` | 新管道/日志/性能/UDP 接口拒绝坏参数后仍正常工作，不泄漏资源或改盘 |
| `make test-topology-host`、`make test-smp` | 固件边界与 1/2/4 核计算、每核时钟/浮点/睡眠/故障回收 |
| `make test-scheduler-host` | 真实生产队列 helper 的保序、核心归属、固定绑定与绝对/相对时钟期限 sanitizer；不执行真实 CR3/CLI/IPI 或汇编切栈 |
| `make test-cooperation` | 1/2/4 核多生产者精确记录、满管道关闭唤醒、内存回收 |
| `make test-network-host`、`make test-network` | 恶意包校验与真实网卡收发、用户 UDP 和网络压力 |
| `make test-log-host`、`make test-performance` | 日志环覆盖、浮点隔离、8/12 进程与管道压力 |
| `make benchmark` | 按性能教程准备 Linux 内核后，同配置 TCG 对照；独立于普通回归 |

已运行的 [最终集成功能记录](./measurements/validation/README.md) 与 [最终 1/2/4 核网络记录](./measurements/network-smp/final/README.md) 标出所测内核摘要和原始输出；阅读时先核对镜像版本，再判断记录覆盖哪些行为。

异常测试的目标是看到预期诊断和测试通过，不是启动后保持交互。系统回归使用自己的数据盘副本，并核对正式 `build/data.img` 未变；不要手工让故障注入实验写个人盘。不要并发运行测试：它们共享构建输出。

日志在 `build/*.serial.log`、`build/system-test/` 与 `build/memory-user-test/`。关注**第一项失败**和它之前最后一个成功标记；最后显示“无法运行 hello”可能只是更早内存/挂载失败的后果。

### 11.2 常见问题

| 现象 | 首先检查 | 处理方向 |
| --- | --- | --- |
| `Missing tool` | `make check-env` 的工具名 | 安装/配置对应 ELF 工具；不要用 macOS ld 代替 |
| 在 os64 输入 make 报未知命令 | 你是否在 `os64 % ` 后 | 回宿主终端执行构建命令 |
| 镜像写锁错误 | 是否已有 QEMU 使用同一 data.img | 正常退出旧实例，或用独立 BUILD_DIR |
| 启动停在某阶段 | 最后一个 stage 成功消息 | 回看该步骤及串口日志 |
| `storage_backend=ram (volatile)` | ATA 是否探测/挂载失败 | 检查数据盘存在和卷错误；先备份，不要直接 reset |
| 新工具找不到/仍是旧输出 | 已有 data.img 中旧 /bin 保留 | 停止 QEMU 后 make update-tools，或用独立实验盘 |
| `run load failed` | 文件是否正确 ELF、大小/段/权限/资源限制 | 看装载器限制与剩余内存；不要只改常量 |
| 目录创建/保存失败 | 已存在、路径长度、inode/数据块不足 | `ls`、`stat`、`disk` 逐项确认 |
| 编辑器读不到输入 | 是否 QEMU 窗口未聚焦，是否已进入用户程序 | 点进窗口/在串口逐行输入；不要同时输入 Shell 命令 |
| fault/ud2 退出非零 | 是否你故意运行故障演示 | 142/134 属于预期，继续运行 hello 检查隔离 |

## 12. 接下来怎样读和扩展

想知道与现代通用系统还有哪些差距，读 [现代操作系统对照](./MODERN_OS_COMPARISON.md)。它区分已有能力与未来目标，并解释单 CPU 上的多进程与真正多 CPU 并行为什么不同。

第一次可分成四次学习，每次留下一项可以复述的结果：

1. 读第 1–3 章，运行 hello，保存一个文件并跨重启验证。复述宿主/客体和三份镜像的区别。
2. 读第 4–5 章，对照 stage1/stage2/entry64。复述一条 C++ 指令之前，启动器必须准备什么。
3. 读第 6–8 章，运行 mem_test、spawn_test、badptr/fault。复述虚拟地址为什么可以相同但内存独立，用户请求为什么要检查。
4. 读第 9–10 章，编辑自己的笔记，再沿着保存接口读到 ATA。复述运行时回滚与断电恢复的区别。

之后按 [文档索引](./README.md) 选对应历史专题，不需要按 40 多篇从头到尾背完。每次读一个函数，记三行：它收到什么，它改变了谁，它失败时如何撤销。看懂这三件事，比记住每个缩写更能帮你改系统。

当前已经有阻塞管道、引用计数描述符、现代 Shell 的常用语法、ARP/IPv4/ICMP/UDP、内核日志，以及 x87/SSE 浮点现场。已有最多四核的 AP 启动和固定核心的用户进程并行；内核操作用大内核锁串行，尚无迁移、跨核共享地址空间/TLB shootdown、用户多线程接口、fork/exec 替换、信号、动态链接、POSIX/Linux ABI、demand paging、完整多用户权限、AVX/XSAVE、TCP/DNS/DHCP/IPv6、图形桌面、UEFI 或现代存储/USB 驱动。不要把“内存上限扩大、编辑器能保存”解释成现代通用 OS 已完成；它们让下一步更大的程序和更真实的失败场景可以在这个教学系统里被验证。

## 13. 从单个程序走到协作和测量

### 13.1 把数据交给另一个程序

在 os64 输入：

```text
echo "one two" | cat | wc
```

期望得到 `1 2 8`：一行、两个词、八个字节（含最后换行）。`echo` 产生字节，`cat` 读到多少就传多少，`wc` 在结束时计数。`|` 不把第一个程序的全部输出先存成文件，而是建立一个内存里的有界字节队列，让三个程序交错运行。

生产者写满 4096 字节会睡眠，读者取走数据后唤醒它；读者遇到空队列时也会睡眠。最后一个写端关闭且队列已经读空才是 EOF。若 Shell 错把多余写端留在其他子进程里，读者会永远等不到结束。因此“先启动全部管段、关闭所有多余端、再等待”是实现中的关键顺序。按 [IPC/Shell 教程](./IPC_SHELL_TUTORIAL.md) 追 `shell/parser.cpp → launch_pipeline → sys_spawn → fd.cpp`。

### 13.2 与系统外面通信

先运行 `net`，期望 `network_ready=1`。再输入 `ping 10.0.2.2`，检查 `ping_received=1`。这里的 `10.0.2.2` 是 QEMU 的网络网关；数据必须经过虚拟网卡、网卡描述符、Ethernet、IP 和 ICMP，已经离开 Shell 的字符串处理层。

没有基础先不要背协议字段。[网络教程](./NETWORK_TUTORIAL.md) 用“给网卡一个缓冲区”“找下一跳地址”“检查消息长度和校验”串起代码，再做宿主 UDP 发包/回显实验。当前是静态地址的 IPv4/UDP 网络，不能因为 ping 通就认为 HTTPS 浏览器已经能运行：浏览器还需要 TCP、DNS、TLS 和更多用户库。

### 13.3 速度必须与正确结果一起看

```text
run /bin/fp_test
run /bin/bench 8 20000000
perf
dmesg
```

`fp_test` 让多个程序使用不同浮点寄存器和控制状态，再交错让出/睡眠/被时钟抢占；只有恢复后的值都正确才算隔离通过。`bench` 让多个子进程计算可独立校验的整数序列，记录完成时间、切换次数和物理页回收。单 CPU 上八个计算进程共享同一个核心；当前四核则可同时执行四个用户计算进程，更多进程继续轮转。内核串行、固定核心和宿主模拟仍限制加速，不能按进程数推算性能。

`perf` 是某一刻的计数快照，两个快照的差才说明期间发生了多少操作；`dmesg` 是时间有序的事件记录。日志只保留最近 256 条，`log_overwritten` 告诉你早期记录是否被覆盖。需要留文件时先创建工作目录，再执行 `logsave /work/kernel.log`。

在宿主运行 `make benchmark` 默认一核；`BENCHMARK_CPUS=2 make benchmark` 或 `4` 指定多核。它会用同一计算源码、相同 QEMU 模拟器/CPU/内存对照 Linux，保存原始样本和报告。第一次先按 [性能教程](./PERFORMANCE_TUTORIAL.md) 下载官方 Linux 实验资产；该命令要求资产已经存在。报告会说明时钟精度、模拟器、进程创建差异和功能条件，不将一个合成循环的名次当成整个系统的名次。计时公式、管道吞吐和下一步优化顺序也在性能教程中。


### 从单核学习走到多核

先在宿主 `OS64_CPUS=4 make run`，在 os64 输入 `smp`、`run /bin/smp_test`、`run /bin/coop_test`，再查看 `smp`。四核应为 online=4、mask=15，工作者 CPU mask 和每核用户计数覆盖实际在线 CPU；只改 QEMU 核数不等于内核真的调度了所有核心。

[AP 启动教程](SMP_BOOT_TUTORIAL.md) 从“固件告诉我们有谁”讲到短汇编跳板和每核入口；[多核调度教程](SMP_SCHEDULER_TUTORIAL.md) 再追 ready 队列、切栈、睡眠和回收。正式 `sleep` 登记等待后切走；不能让 AP 持内核锁直接 HLT 等 BSP 的全局时间。启动中的历史抢占自测用独立 helper 刻意保持 Running，测试 CPU 记账；它不是应用睡眠接口。

网络的 `udp_receive_wait` 可以指定 0、有限毫秒或无限等待。接收进程可在 AP 上睡眠，BSP 网卡工作线程收到包后跨核唤醒它；详见[网络教程](NETWORK_TUTORIAL.md)。当前共享的是管道/句柄引用，不开放通用用户共享内存或同地址空间的多个用户线程。
