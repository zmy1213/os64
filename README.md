# os64

一个从自写 BIOS 启动器开始的 x86_64 教学操作系统。内核和用户程序都直接运行在 QEMU 的虚拟 CPU 上，不依赖宿主操作系统的进程、文件接口或 C 标准库。

目前已连通从启动到日常交互的完整教学流程：64 位内核、物理页和内核堆、用户地址空间、时钟抢占与阻塞调度、ELF 用户进程、系统调用、可保存到磁盘的文件系统、键盘/串口终端和 Shell。

目标平台是 QEMU 的单核传统 PC：BIOS、VGA 文本显示、8259 PIC、PIT、PS/2 键盘、16550 串口和 IDE ATA 数据盘。

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
ps
mem
sync
reboot
run /bin/cat /saved.txt
shutdown
```

`run` 在独立的 ring 3 地址空间中运行 ELF 程序，继承当前目录，传入参数，等待退出并打印退出码，随后回收进程资源。支持最多 8 个参数（包含程序名），每个参数最多 63 字节；引号可以保留参数中的空格。用户程序也可以通过 `spawn`、`waitpid` 创建并等待子进程。

`run /bin/fault` 与 `run /bin/ud2` 用于演示用户页错误和非法指令隔离；退出后 Shell 继续工作。`run /bin/badptr` 演示系统调用拒绝错误指针。`run /bin/fs_test /large.txt` 验证跨直接块和间接块的文件读写。

常用内建命令：

| 用途 | 命令 |
| --- | --- |
| 文件和目录 | `pwd`、`cd`、`ls`、`cat`、`stat`、`touch`、`mkdir`、`write`、`append`、`rm`、`sync` |
| 程序与进程 | `run`、`ps` |
| 系统信息 | `mem`、`heap`、`disk`、`ticks`、`uptime`、`irq`、`bootinfo`、`e820`、`cpu` |
| 终端与电源 | `help`、`echo`、`history`、`clear`、`reboot`、`shutdown` |

用户程序及 ABI 见 [user/README.md](user/README.md)。

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

首次构建创建 `data.img`。之后的构建和 `make clean` 保留它，已有文件不会被模板覆盖。更新内核不需要重置数据盘；用户工具更新后，已有数据盘里的旧工具也会被保留。

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

系统测试使用自己的临时数据盘，并检查用户 `build/data.img` 的摘要保持不变。日志位于 `build/system-test/` 和 `build/*.serial.log`。各项也可以单独运行：`make test-stage1`、`make test-system`、`make test-storage-host`、`make test-page-fault`、`make test-invalid-opcode`。

## 当前边界

这是能独立启动、运行用户程序并保存文件的教学系统。它使用固定资源上限：8 个进程、16 条线程、4–8 MiB 用户虚拟窗口、单页用户栈；ELF 文件最多 4096 字节，最多 8 个程序段、32 张装载页。内核堆位于 16–20 MiB，正式运行前预映射，以保证进程共享的内核映射一致。

文件系统数据盘当前有 64 个 inode 槽位；格式最多支持 128 个 inode 槽位，单文件最多 69,632 字节。`rm` 可以删除文件和空目录，仍打开的文件与活动工作目录不能删除。

用户态支持时钟抢占；内核线程在受控的睡眠、阻塞和让出点调度。尚未实现 SMP、多用户权限、`fork`、信号、管道、动态链接、虚拟内存换页、FPU/SIMD 上下文、网络协议栈、图形桌面、USB/AHCI/NVMe 或 UEFI，不能运行 Linux 二进制，也没有做真实硬件兼容认证。

## 阅读实现

- [启动到用户态的原版讲解](docs/BOOT_TO_USERLAND_WALKTHROUGH.md)：保留最初的逐步教程。
- [原有专题索引](docs/README.md)：汇编启动、页表、调度器、Shell 与系统调用。
- [进程运行与回收](docs/PROCESS_RUNTIME.md)：新的进程生命周期和异常隔离。
- [持久化存储](docs/PERSISTENT_STORAGE.md)：ATA、OS64FS 写入、失败回滚与限制。
- [用户程序](user/README.md)：入口栈、ABI 和编译方式。

目录分工：`boot/` 自写启动器，`kernel/` 内核，`user/` 用户工具，`scripts/` 构建与运行测试，`tests/` 宿主故障测试，`docs/` 教程。
