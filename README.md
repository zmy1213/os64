# os64

一个从自写 BIOS 启动器开始的 x86_64 教学操作系统。内核和用户程序都直接运行在 QEMU 的虚拟 CPU 上，不依赖宿主操作系统的进程、文件接口或 C 标准库。

目前已连通从启动到日常交互的完整教学流程：64 位内核、可回收物理页和内核堆、用户地址空间与动态堆、时钟抢占与阻塞调度、ELF 用户进程、系统调用、可保存到磁盘的文件系统、键盘/串口终端、Shell 和用户态行式文本编辑器。

目标平台是 QEMU 的单核传统 PC：BIOS、VGA 文本显示、8259 PIC、PIT、PS/2 键盘、16550 串口、IDE ATA 数据盘和 PCI virtio-net 网卡。当前已有阻塞管道、描述符继承、UDP 网络、内核日志与多进程压力测试；性能结果按实测记录。

## 从零开始读

没有操作系统基础，先读 **[从零开始的当前版本教程](docs/BEGINNER_TUTORIAL.md)**。它从宿主/客体、工具安装、第一次启动和安全备份讲起，再按真实函数追启动、内存、用户程序、系统调用和文件保存，最后用编辑器完成一个实际应用实验。

[源码阅读地图](docs/BEGINNER_SOURCE_MAP.md) 帮你定位文件；[文档索引](docs/README.md) 区分当前说明与历史专题。旧教程正文中的“当前”“这一轮”指对应开发阶段，不能作为最新功能和地址布局清单。

## 快速启动

在仓库根目录执行：

```sh
make check-env
make build
make run-gui
```

`make run-gui` 打开 QEMU 窗口，在窗口中输入命令。若希望在当前终端交互，运行 `make run`；串口支持文字输入、方向键、历史和删除编辑。退出模拟器可以关闭窗口，或在系统 Shell 中输入 `shutdown`。

工具要求：NASM、QEMU、Python 3、Clang++、ELF 链接器 LLD 和 LLVM objcopy。也支持 `x86_64-elf-g++/ld/objcopy` 交叉工具链。脚本自动发现 PATH 和 macOS Homebrew LLVM，不使用宿主 macOS 链接器。Linux 可以安装：

```sh
sudo apt-get install clang lld llvm nasm qemu-system-x86 python3
```

需要手动指定工具时可以设置 `CLANGXX_BIN`、`LD_BIN`、`OBJCOPY_BIN`、`NASM_BIN`、`QEMU_BIN` 和 `PYTHON_BIN`。具体发现规则在 `scripts/toolchain.sh`。

## 在系统里做什么

提示符为 `os64 % `。每一行是系统内的命令。

```text
help
ls /bin
run /bin/hello alpha beta
run /bin/echo "hello world"
run /bin/ls /docs
run /bin/cat /readme.txt
mkdir /work
write /work/note.txt hello
append /work/note.txt _os64
cat /work/note.txt
run /bin/writer /saved.txt saved_after_reboot
run /bin/spawn_test
run /bin/mem_test
ps
mem
sync
reboot
run /bin/cat /saved.txt
shutdown
```

`run` 在独立的 ring 3 地址空间中运行 ELF 程序，继承当前目录，传入参数，等待退出并打印退出码，随后回收进程资源。支持最多 8 个参数（包含程序名），每个参数最多 63 字节；引号可以保留参数中的空格。用户程序也可以通过 `spawn`、`waitpid` 创建并等待子进程。

`run /bin/fault` 与 `run /bin/ud2` 用于演示用户页错误和非法指令隔离；退出后 Shell 继续工作。`run /bin/badptr` 演示系统调用拒绝错误指针。`run /bin/fs_test /large.txt` 验证跨直接块和间接块的文件读写。

常用命令（普通工具在用户态运行，系统观察和 cwd 控制由 Shell 提供）：

| 用途 | 命令 |
| --- | --- |
| 文件和目录 | `pwd`、`cd`、`ls`、`cat`、`stat`、`touch`、`mkdir`、`write`、`append`、`rm`、`sync` |
| 程序与进程 | 直接输入 `/bin` 中命令名、`run`、`ps`、`jobs`、`wait` |
| 系统信息 | `mem`、`heap`、`disk`、`ticks`、`uptime`、`irq`、`bootinfo`、`e820`、`cpu`、`perf`、`dmesg`、`logsave` |
| 网络 | `net`、`ping IPv4`、用户 `udp_test` |
| 终端与电源 | `help`、`echo`、`history`、`clear`、`reboot`、`shutdown` |

在 Shell 单独运行 `run /bin/edit /work/lesson.txt`。编辑器运行后，在 `edit> ` 输入 `append hello`、`print`、`save`、`quit`；回到 Shell 后 `cat /work/lesson.txt`。未保存时 `quit` 会提醒，`quit!` 丢弃内存修改。它是逐行操作的文本编辑器，最大文本 32 KiB，不是全屏编辑器。用户程序、内存库和 ABI 见 [user/README.md](user/README.md)。

## 进程协作、网络与性能实验

在 os64 中可直接执行以下例子：

```text
echo "one two" | cat | wc
echo hello > /work/output.txt
echo again >> /work/output.txt
cat < /work/output.txt | wc
false && echo skipped || echo recovered
echo $?
sleep 100 &
jobs
wait
run /bin/pipe_test
run /bin/fp_test
run /bin/bench 8 20000000
perf
dmesg
logsave /work/kernel.log
net
ping 10.0.2.2
```

Shell 在 `/bin` 查找不带斜线的命令；支持引号、转义、`$?`/`$PWD`/固定 `$PATH`、四段管道、输入/输出/错误重定向、条件执行及简单后台任务。每段在独立用户进程中运行，所有管段先启动再等待；文件位置与管道端点经引用计数共享。这里还没有完整 POSIX 脚本语言、环境变量表、通配符、信号或 TTY 作业控制，详见 [进程协作与 Shell 教程](docs/IPC_SHELL_TUTORIAL.md)。

默认启动脚本添加 virtio-net legacy PCI 网卡和 QEMU user 网络，客体地址 `10.0.2.15`，宿主 `127.0.0.1:5555` 转发到客体 UDP 回显端口 9000。真实驱动通过 DMA 描述符队列传递 Ethernet 帧，协议实现 ARP、IPv4、ICMP 和 UDP；用户 UDP 句柄按进程隔离并在退出时关闭。没有网卡仍能启动。TCP、DNS、DHCP、IPv6、Wi-Fi 尚未实现，配置和从零实验见 [网络教程](docs/NETWORK_TUTORIAL.md)。

内核默认 `-O2` 编译，可用 `KERNEL_OPT_LEVEL=0 make build` 保留便于逐句调试的版本。x87、SSE/SSE2 的浮点现场通过 FXSAVE/FXRSTOR 随线程保存；内核仍用通用寄存器编译，尚未开放 AVX/XSAVE。日志是有界 256 条内存环，带序号、时钟、等级和组件，覆盖最旧记录会计数；不在 IRQ 热路径同步写串口或磁盘，`logsave` 才主动保存。

[性能教程](docs/PERFORMANCE_TUTORIAL.md) 说明如何校验计算结果、测多进程与管道、读取调度计数，并在相同 QEMU/单 CPU/128 MiB 条件下对照 Linux。性能指标分别解释吞吐、延迟、尾延迟、丢包和资源回收；任何结果都不能直接外推为“所有工作负载最快”。

## 镜像和保存

构建输出位于 `build/`：

| 文件 | 作用 |
| --- | --- |
| `disk.img` | 1.44 MiB BIOS 启动软盘，包含自写 stage1、stage2、内核和启动自测卷 |
| `data.img` | 512 KiB IDE 数据盘，保存交互系统的目录、文件和 `/bin` 工具 |
| `data_volume.bin` | 干净数据盘模板 |
| `boot_volume.bin` | 启动自测使用的固定 RAM 卷模板 |
| `kernel.elf`、`kernel.bin` | 可调试的内核 ELF 与裸内核映像 |
| `user/` | 独立编译的用户 ELF 文件 |
| `build-manifest.json` | 实际优化级、IRQ 构建开关、工具版本、源码与镜像/用户程序摘要，用于复核性能实验 |

首次构建创建 `data.img`。之后的构建和 `make clean` 保留它，clean 也保留 `data.img.backup-*`，已有文件不会被模板覆盖。更新内核不需要重置数据盘；用户工具重新编译后，盘里的旧工具也会保留。

已有数据盘需要安装新版 `/bin` 时，先在 os64 中 `shutdown` 并确认 QEMU 已退出，然后在宿主运行：

```sh
make update-tools
```

它使用真实 OS64FS 实现在副本上只更新 `/bin`，检查成功后才替换数据镜像，保留其他文件并创建完整的 `data.img.backup-UTC时间戳` 备份。磁盘在用、空间不足或校验失败时拒绝更新。该目标更新默认 `build/data.img`，不要与运行中的 QEMU 同时执行。

`make reset-data` 会用新模板覆盖数据盘，删除其中保存的文件；`make distclean` 删除全部构建产物和数据盘。要保留文件，先备份 `build/data.img`。

内核先在 RAM 自测卷上完成回归检查，再挂载 IDE 数据盘。启动日志中的 `storage_backend=ata-pio` 表示真正保存到数据盘；`storage_backend=ram (volatile)` 表示没有可挂载的数据盘，文件只在本次启动中存在。系统不会自动格式化一个损坏的数据盘。

成功返回的文件变更已经写回并刷新 ATA 缓存。当前没有磁盘日志或断电恢复，写入过程中强制结束模拟器可能损坏数据卷。存储实现与边界见 [持久化存储说明](docs/PERSISTENT_STORAGE.md)。

## 验证

```sh
make test
```

测试包括：

- 自写启动链、内存、系统调用、ring 3、抢占与键盘的原始回归。
- QMP 真实键盘输入执行用户程序，检查参数、进程创建/等待、错误指针、故障隔离、睡眠与计算程序。
- 连续运行 55 次程序，比较空闲物理页数量，检查回收。
- 三次冷启动，检查保存文件、跨间接块内容和删除结果。
- 内核页错误和非法指令的诊断路径。
- 宿主 ASan/UBSan 存储测试，检查空间不足、坏元数据、部分 I/O 失败与回滚。
- 宿主物理页位图测试，以及 QEMU 大于旧 4 KiB 上限的 ELF、1 MiB 用户堆、缩堆清零、重复回收、64 KiB 栈、guard/NX、编辑器操作和冷启动保存测试。
- Shell 解析器 sanitizer、真实多进程管道/EOF/EPIPE/共享偏移/小写入原子性/重定向、超长整行拒绝与重复释放。
- 网络协议恶意帧与校验测试、实际 virtio ARP/ICMP/UDP、宿主回显、用户 UDP 权限/退出清理、丢包与 RTT 记录。
- 管道、日志/性能、UDP 系统调用的坏指针和整数边界；拒绝后功能仍可用，资源和磁盘保持稳定。
- 日志环覆盖/边界 sanitizer、x87/SSE 初始值与切换隔离、8/12 工作者计算、8 进程定时进度、32 MiB 管道校验，以及日志保存后的冷启动读取。

系统测试使用自己的临时数据盘，并检查用户 `build/data.img` 的摘要保持不变。日志位于 `build/system-test/`、`build/memory-user-test/`、`build/ipc-test/`、`build/syscall-boundary-test/`、`build/network-test/`、`build/performance-results/` 和 `build/*.serial.log`。单项入口及预期现象见 [主教程第 11 章验证表](docs/BEGINNER_TUTORIAL.md)。

`make benchmark` 是独立的 Linux 对照实验，按 [性能教程](docs/PERFORMANCE_TUTORIAL.md) 准备官方 Linux 资产后执行，不包含在普通回归中。它会逐项检查正确性和计算机器码，再输出原始样本与统计。

## 当前边界

这是能独立启动、运行用户程序并保存文件的教学系统。物理页管理覆盖 E820 可用区间中的低 256 MiB，通过 `0xffff800000000000` 起的内核直接映射访问，分配页可回收；超过该管理上限的内存暂不使用，默认 QEMU RAM 为 128 MiB。

固定上限仍有：16 个进程、32 条线程、4–8 MiB 用户窗口。用户栈是 64 KiB（`0x7f0000`–`0x800000`），下方有一页不映射的 guard；动态堆由 `brk` 立即分配，不能越过 `0x7ef000`。ELF 文件最多 69,632 字节、8 个程序头、256 张装载页，拒绝 RWX 段和占用栈区域的段。支持 NX 的 CPU 上代码不可写，数据/堆/栈不可执行；启动日志报告 `nx_enabled`。内核堆仍固定在 16–20 MiB，预映射并由进程共享。

文件系统数据盘当前有 64 个 inode 槽位；格式最多支持 128 个 inode 槽位，单文件最多 69,632 字节。`rm` 可以删除文件和空目录，仍打开的文件与活动工作目录不能删除。

用户态支持时钟抢占；内核线程在受控的睡眠、阻塞和让出点调度。尚未实现 SMP、多用户权限、`fork`/`exec` 替换、信号、动态链接、demand paging/换页、AVX/XSAVE、TCP/IPv6、图形桌面、USB/AHCI/NVMe 或 UEFI，不能运行 Linux 二进制，也没有做真实硬件兼容认证。用户栈保护不代表所有内核栈都有 guard；运行时文件事务不提供断电一致性。

[现代操作系统对照](docs/MODERN_OS_COMPARISON.md) 按已实现/未实现逐项列出差距，并以多进程计算与调度为重点给出后续五阶段验收。单核增加进程数让任务轮流运行，真正的多核并行还需要 AP 启动、每 CPU 状态、跨核同步、IPI 和 TLB shootdown。

## 阅读实现

- [当前版本主教程](docs/BEGINNER_TUTORIAL.md)：从第一次运行到实际源码和实验。
- [启动到用户态的原版讲解](docs/BOOT_TO_USERLAND_WALKTHROUGH.md)：保留早期开发过程，已标记适用阶段。
- [专题索引](docs/README.md)：当前阅读路径与历史原理补充。
- [进程运行与回收](docs/PROCESS_RUNTIME.md)：新的进程生命周期和异常隔离。
- [持久化存储](docs/PERSISTENT_STORAGE.md)：ATA、OS64FS 写入、失败回滚与限制。
- [用户程序](user/README.md)：入口栈、ABI 和编译方式。

目录分工：`boot/` 自写启动器，`kernel/` 内核，`user/` 用户工具，`scripts/` 构建与运行测试，`tests/` 宿主故障测试，`docs/` 教程。
