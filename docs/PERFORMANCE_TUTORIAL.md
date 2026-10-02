# 从零理解 CPU、调度、日志和性能测量

这篇接在 [主教程](./BEGINNER_TUTORIAL.md) 后面。先能启动 OS64、运行一个程序，再做这里的实验。先看现象，再追到源码，不需要先学完浮点指令或统计学。

本轮目标是让多个计算进程正确地轮流运行，保护它们各自的 x87/SSE 寄存器，给内核添加有界日志，并用可重复的实验判断改动是否有效。当前仍只有一个 CPU 核运行 OS64。十二个进程是在一颗 CPU 上轮流工作，不会凭空获得十二颗 CPU 的计算能力。

## 1. 第一次观察：三个命令分别告诉你什么

在宿主机仓库根目录构建。已有数据盘时先在 QEMU 中 `shutdown`，再更新工具：

```sh
make build
make update-tools
make run
```

首次构建会创建数据盘，可以直接 `make run`。不要用 `reset-data` 代替更新，它会丢弃原数据盘的文件。下面是 **QEMU 中 OS64 的命令**，不是宿主终端命令：

```text
perf
dmesg
run /bin/bench 4 30000000 0
```

`perf` 是瞬时状态表：

| 输出 | 如何理解 |
|---|---|
| `cpu_brand` | 客体通过 CPUID 看到的 CPU 名称；不能当成宿主型号 |
| `cpu_fpu_sse_state=1` | 内核已经启用并隔离 x87/SSE 现场 |
| `perf_ticks`、`perf_timer_hz` | 启动以来的节拍与每秒节拍数；目前 100 Hz |
| `perf_switches` | 已完成的线程切换次数 |
| `perf_yields` | 主动让出 CPU 的次数 |
| `perf_preempt_requests` | 定时器抢占请求次数；请求不一定造成切换 |
| `perf_free_pages` | 当前空闲物理页数；一页是 4096 字节 |
| `perf_heap_used_bytes` | 内核堆正在使用的字节数，不是全部用户堆之和 |
| `perf_live_threads` 等 | 活跃、就绪、阻塞、睡眠线程当前数量 |
| `perf_log_next_sequence`、`perf_log_overwritten` | 下一日志序号、被新记录覆盖的旧记录数量 |
| `perf_cpu_features` | CPU 能力的位集合，见后面的特性表 |
| `perf_tsc_raw` | 原始虚拟 TSC 数值，没有换算成纳秒 |

`dmesg` 读取内存日志。每行依次是序号、tick、等级、组件、消息，例如：

```text
1 0 info boot 64-bit kernel entered
```

真实 tick、行数和网络提示可能不同。没有网络设备的基准 VM 会报告相应提示，这不影响计算实验。

`bench` 启动四个子进程，每个做三千万次整数更新，再等待它们全部结束。应看到 `bench correctness=ok` 和 `run_exit_code=0`。耗时、切换数和空闲页数都是实测值，不能把文档数字当成每次相同的答案。

## 2. 先检查正确性，再讨论快慢

一个程序本来应计算一亿次，错误版本只算一次，当然很快；这种“提升”没有意义。工作量在 [bench_workload.hpp](../user/bench_workload.hpp)：

```cpp
state = state * 6364136223846793005ULL + 1442695040888963407ULL;
```

下一次使用上一次结果，有明确的依赖。`uint64_t` 溢出按模 2⁶⁴ 计算，是 C++ 对无符号整数规定的行为，不是未定义行为。

另一种算法 `bench_expected` 把连续更新合成 `a * state + b`，按更新次数的二进制展开计算，只需约 log₂(N) 步。它独立检查直接循环有没有少算、重复或算错，不需要再运行一次同样长的循环。

每个工作者先检查完整 64 位结果，再退出。Linux 的普通退出状态只有八位，因此父进程通过八位摘要收集结果。摘要用 0–250，252–255 留给错误。摘要相同不等于只做了八位正确性检查。

宿主机 `make test-log-host` 用许多短循环验证两种算法一致。大型基准仍逐个检查结果，不能关闭检查换取更好看的速度。

## 3. 一颗 CPU 上的多进程

进程是运行中的程序及它的资源，线程是调度器安排执行的单位。目前每个用户进程有一个主线程。进程有各自的用户页表、堆、栈和描述符视图，轮流使用同一 CPU。

就绪队列像等待 CPU 的队伍。轮到一个线程后，它可能继续计算并被定时器抢占，主动 `yield()`，等待子进程或管道而阻塞，调用 `sleep()`，或者退出。阻塞者只有条件满足才重新排队。

从 [scheduler.cpp](../kernel/task/scheduler.cpp) 的 `scheduler_yield`、`scheduler_preempt`、阻塞/唤醒函数追到 [context_switch.asm](../kernel/task/context_switch.asm)。切换必须保存当前寄存器和栈指针、切换地址空间并恢复目标现场。只修改线程编号不能让两个 C++ 调用栈继续执行。

用户计算可被定时器抢占；内核工作仍依赖明确的调度点，一次很长的内核操作可能延迟其他任务。增加进程表槽位也不能消除这种延迟。当前 PCB 上限 16、TCB 上限 32，Shell/idle 等也占资源；`bench` 支持 1–12 个工作者。

调度器已有粗粒度优先级，在同优先级就绪队列中轮转。本文计算工作者使用相同默认优先级，实验主要观察这种情形。

**练习：保持总工作量一致。**

```text
run /bin/bench 1 120000000 0
run /bin/bench 4 30000000 0
run /bin/bench 12 10000000 0
```

三次都是一亿两千万次更新。观察耗时和切换数。在单 CPU 中增加工作者通常增加创建与调度成本，不应期待十二倍加速。

下面只改变主动让出 CPU 的频率：

```text
run /bin/bench 4 30000000 65536
```

每计算 65,536 次 yield 一次。更多切换可能改善响应性，也可能降低吞吐量，不能把切换次数越大当成越快。

## 4. 怎样检查“大家都有进展”

全部进程最终结束说明没有永久卡住，但不足以证明它们得到相同 CPU 时间。

```text
run /bin/sched_test
```

八个工作者等待同一个开始时刻，再各做六轮大计算。计算阶段不调用 yield；每轮结束向共享管道写一条 32 字节进度记录。父进程检查轮次、时间顺序和完整 64 位结果，要求第一个工作者完成全部工作前，其余七个都已报告计算进度。

预期：

```text
sched_test eight_workers_progress_before_first_exit_ok
run_exit_code=0
```

这是定时器抢占、基本进度和资源回收的有限测试。它没有证明任意负载下的响应时间上限、严格 CPU 份额公平性、不同优先级间的公平性或实时保证。

## 5. SSE/FPU 也必须随线程保存

除了 RAX、RSP 等通用寄存器，CPU 还有 x87 浮点寄存器和 XMM 寄存器。XMM 可以保存浮点或成组整数，同样属于当前线程的现场。

A 把数据放入 XMM0，切换到 B 后 B 改写 XMM0，恢复 A 却不还原，A 就会读到 B 的数据。保存通用寄存器不能解决这个错误。

[cpu_initialize](../kernel/cpu/cpu.cpp) 用 CPUID 检查 FPU、FXSAVE、SSE、SSE2，再设置控制寄存器启用它们。初始模板有默认 x87 控制字 `0x037f`、空标签、清零的 x87 数据、MXCSR `0x1f80` 和清零的 XMM0–15。新线程复制模板，避免继承旧线程或固件的数据。

每个 TCB 有 16 字节对齐、512 字节的 `CpuFloatingState`。对齐是 CPU 指令要求，不可随意放在不对齐地址；bootstrap 也有独立副本。

当前调度器调用 `scheduler_switch_context_and_root`。文件开头的
`scheduler_switch_context` 是保留的早期入口，当前 C++ 调度器不调用它；
读源码时应定位到带 `_and_root` 的版本。它先关闭中断：

```asm
fxsave64 [rcx]   ; 保存当前 x87/XMM/控制状态
fxrstor64 [r8]   ; 恢复目标线程状态
```

再执行通用寄存器、RSP、CR3、RFLAGS 切换，恢复目标原来的中断状态。不能在保存一半时被定时器触发第二轮切换。

```text
run /bin/fp_test
```

预期 `fp_test SSE_x87_isolation_ok`、退出码 0。四个工作者用不同的 XMM0/XMM15、x87 累加值、控制字和 MXCSR，同时经历定时器抢占、yield、sleep，逐轮检查各自的值，也检查新线程初始清零。

内核和普通用户工具仍以 `-mgeneral-regs-only` 编译，让编译器不自动在这些路径使用向量/浮点寄存器；`fp_test` 用明确汇编。已有现场保护不等于有完整浮点 C 库。

AVX 比 XMM 更宽，FXSAVE 保存不了全部内容。目前没有 OSXSAVE/XSAVE/XRSTOR，不能宣称支持 AVX。以后开放自动 SIMD 编译还需审查中断、内核与用户入口的现场约定。

| `perf_cpu_features` 位 | 十进制值 | CPUID 能力 |
|---:|---:|---|
| 0 | 1 | x87 FPU |
| 1 | 2 | FXSAVE/FXRSTOR |
| 2 | 4 | SSE |
| 3 | 8 | SSE2 |
| 4 | 16 | TSC |
| 5 | 32 | NX |
| 6 | 64 | invariant TSC |

这是能力集合；是否真正启用、如何实施权限，还需检查相应初始化，不能只看一个数字。

## 6. 有界日志：不要每个中断都打印

串口、磁盘可能很慢。频繁中断里大量打印，会把调度时间变成输出时间，也会污染实验。

[kernel_log_write](../kernel/log/log.cpp) 只向预分配内存环写固定记录，不分配堆、不阻塞、不格式化数字、不调用串口或磁盘。每条 128 字节，共 256 条，占 32 KiB。组件最多保存 15 字节，消息最多 87 字节，保留字符串终止符。

环满后覆盖最旧记录，增加 `overwritten`。容量有明确上限，不能声称启动以来的所有日志永久保留。

读写短暂关闭本 CPU 中断，并保存原 IF；离开时只有原先开中断才恢复。因此 IRQ 中调用不会错误开中断。这是单 CPU 保护，未来 SMP 的另一个 CPU 不受本 CPU CLI 影响，必须重新设计并发同步。

```text
dmesg
logsave /kernel.log
cat /kernel.log
```

`logsave` 先复制快照，离开临界区后再格式化、保存、sync。预期 `logsave ok`。只保存当时环内记录，后续不会自动追加。内存日志重启清空，保存的文件留在数据盘。

保存用 `replace_file` 运行中事务保护旧文件，但没有断电日志保证。失败先检查空间、路径，再重试，不能当成已持久保存。

syscall 31 允许用户分批读：`read_log(records,count,after_sequence)` 返回序号大于游标的记录，单次最多 64 条。下一批使用上批最后序号。旧记录被覆盖时从仍保留的最早记录开始，应结合序号差和覆盖计数识别丢失。

```cpp
KernelLogRecord records[8];
int64_t count = read_log(records, 8, 0);
// count > 0 时，records[count - 1].sequence 是下一批的游标。
```

syscall 30 返回 ticks，32 写出 128 字节 `PerformanceSnapshot`。输出缓冲每页都检查 present/user/writable，拒绝内核地址、未映射地址、只读代码页、超大日志批次。

```text
run /bin/perf_test
```

预期 `perf_test snapshot_logs_bad_pointers_ok`、退出码 0。宿主 `make test-log-host` 检查环排序、覆盖、截断、游标和边界。sanitizer 检查内存行为，宿主不会执行真正 CLI；真实客体测试覆盖硬件路径。

## 7. 管道基准：正确传输，再测吞吐

```text
run /bin/bench_ipc 33554432
```

父进程传 32 MiB，子进程逐字节验证固定模式直到 EOF。父子要关闭不用的端，否则多余写端会让读取者永远等 EOF。见 [IPC 与 Shell 教程](./IPC_SHELL_TUTORIAL.md)。

预期 `ipc correctness=ok`、`ipc pipe_capacity=4096`、退出码 0。耗时包含创建、阻塞/唤醒、复制和逐字节检查，不是单独复制速度或磁盘吞吐。

环满时写者阻塞；空且仍有写端时读者阻塞；全部写端关闭后得到 EOF。最多 4096 字节的小写入等待整块空间后一次完成，避免多个写者的短记录交错；更大写入可以分段。环首尾最多两段 copy，减少逐字节临界区处理成本。

回归检查结束后空闲物理页数恢复。这个计数无法证明所有资源无泄漏，还需要描述符、管道引用、退出回收测试。

## 8. 耗时与 p50 / p99 怎么读

100 Hz 的一个 tick 约 10 毫秒：`秒数 = elapsed_ticks / timer_hz`。只用 1 tick 的任务，多一个或少一个 tick 就差很多，所以正式计算使用更长的固定总量，不能拿打印一行或几百次循环作排名。

31 个样本按耗时排序：

- p50 是第 16 个，描述通常一次要多久。
- 脚本取 `ceil(0.99 × N)` 的位置作为 p99，N=31 时是第 31 个，即最大值。
- 平均值会受很慢样本影响；最小值只描述最好一次。

31 样本的最大值能发现偶发停顿，不能可靠估计生产系统的百分之一尾部。宿主后台任务、其他回归、温度和调度都能影响它。不要拿最小一次冒充通常性能，也不要把离群点当成必然上限。

本基准的耗时从父进程开始创建工作者，到最后等待/回收完成；
p50/p99 描述整批任务完成时间，包含进程启动成本。它没有测单个交互请求、
中断响应或每个工作者等待 CPU 的延迟，不能把报告的 p99 叫作调度器的 p99 响应。

吞吐量是每秒做多少工作，延迟是一个任务等多久。策略可能提高吞吐却延长短任务等待，要同时检查二者。

Linux 切换计数来自 `getrusage(SELF)+getrusage(CHILDREN)`；OS64 是全调度器线程计数，范围不同。保留原始值，但不能直接用两个数判断谁切换更高效。

TSC 只有原始诊断数值，没有校准虚拟频率或建立跨 CPU 一致性，不能根据宿主主频擅自换算成纳秒。

## 9. 可重复的 Linux 客体对照

这里真正启动 x86_64 Linux，使用官方 Alpine netboot kernel/initramfs，不拿 macOS 宿主充当 Linux。两边同一 QEMU、TCG、qemu64、1 vCPU、128 MiB，无图形。启动不在计时区间，任一时刻只运行一个基准客体。

两边编译相同的 [bench.cpp](../user/programs/bench.cpp)、[bench_ipc.cpp](../user/programs/bench_ipc.cpp) 和工作量函数。Linux 用 [linux_adapter.hpp](../tools/linux_adapter.hpp) 替换 syscall/入口，不用 libc。用户都是 `-Os`、freestanding、通用寄存器编译。脚本比较两边 `bench_compute` 真实机器码，不一致直接停止。

OS64 内核 `-O2`，Linux 是官方二进制，内核构建选项和功能集合仍然不同。这是两个具体客体对这些工作量的比较，不是现代 OS 整体性能与功能排名。

宿主准备资产：

```sh
mkdir -p build/benchmark-deps
curl -fL https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/vmlinuz-virt -o build/benchmark-deps/vmlinuz-virt
curl -fL https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/initramfs-virt -o build/benchmark-deps/initramfs-virt
KERNEL_OPT_LEVEL=2 make benchmark
```

官方入口：[Alpine x86_64 netboot](https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/netboot/)。`latest-stable` 会变，每次记录实际版本与 SHA-256，不能假定未来下载字节与本次相同。普通 `make test` 不下载 Linux；`make benchmark` 要求资产已存在。

每场景每客体默认 31 个测量样本，每个分块先预热一次且不计入统计。顺序 OS64 → Linux → Linux → OS64，16/15 样本分块，缓解宿主负载随时间变化的单向偏差，无法消除全部噪声。

正式 CPU 场景保持总 4.8 亿次更新：

| 场景 | 工作者 | 每个更新数 | 主动 yield |
|---|---:|---:|---|
| single | 1 | 480,000,000 | 无 |
| four | 4 | 120,000,000 | 无 |
| twelve | 12 | 40,000,000 | 无 |
| yield | 4 | 120,000,000 | 每 65,536 次 |
| pipe | 父/子各一 | 32 MiB 传输并验证 | 空/满管道阻塞 |

Linux 用 `F_SETPIPE_SZ` 设为 4096 字节，与 OS64 相同，失败则拒绝测量。OS64 spawn 创建新进程，Linux 适配用 fork+execve，包含各自实现的创建成本。计算工作者不输出字符，避免串口主导耗时。

Linux monotonic clock 换算为 100 Hz，OS64 用 PIT ticks；时钟实现不同。脚本保存宿主命令耗时作诊断，不能把相同单位说成相同硬件。

输出在 `build/performance-results/`：

| 文件 | 用途 |
|---|---|
| `REPORT.md` | p50、p99、吞吐汇总 |
| `results.json` | 原始样本、实际构建、二进制/资产哈希、参数、内核版本 |
| `*.serial.log` | 完整客体输出，查缺失标记 |
| `*.qemu.log` | QEMU 启动与异常诊断 |

只挂载临时数据盘，最后检查原 `data.img` 哈希不变。追加的基准 initramfs 是另一文件，不修改官方资产。

调试可用 `BENCHMARK_SAMPLES=3 make benchmark`，三个样本只能检查联通/正确性，不应报告可靠 p99。独立构建例子：

```sh
BUILD_DIR=/tmp/os64-benchmark KERNEL_OPT_LEVEL=2 make build
BUILD_DIR=/tmp/os64-benchmark BENCHMARK_ASSETS="$PWD/build/benchmark-deps" bash scripts/benchmark.sh
```

## 10. 排错与下一步目标

找不到新 `/bin` 工具：关闭 QEMU 后 `make update-tools`，不要删盘修复。

正确性失败或页数减少：先查串口、退出码、管道关闭和回收；结果错误时没有可报告的分数。

“Workload too short”：增大工作量，100 Hz 未测到一个节拍。正式场景较长，极快环境的快测仍可能需要调大。

Linux 启动失败：查 QEMU/串口日志，确认 x86_64 virt 资产完整、追加 initramfs 成功，不能偷偷换成宿主数据继续叫 Linux 对照。

构建记录不符：重构建，确保 disk.img、bench.elf、build-manifest.json 来自同一轮。比较要求内核 `-O2`、用户 `-Os`，不要混入 O0。

实际目标是在正确性和回收稳定的条件下减少创建、页表、管道复制、无效唤醒成本，并观察进度与响应。不能靠丢日志、少计算、破坏隔离提速。

SMP 还缺 AP 启动、每 CPU 栈/状态/队列、跨 CPU 锁/原子操作、IPI、TLB shootdown、迁移和中断路由。单 CPU CLI 日志锁、全局队列和 FPU 切换不能直接复制成多核系统。以后需要并行吞吐、负载均衡、竞争和隔离测试。

完整系统性能还需要磁盘、网络、内存压力与交互实验。本篇建立 CPU、调度和管道的第一份可复核基线。

## 11. 本轮实际结果：2026-10-02

已经真正执行了两客体各场景 31 次测量，共 310 个正式样本，另有预热和独立正确性测试。环境是 QEMU 10.2.0、TCG、qemu64、单 vCPU、128 MiB；Linux 为 `6.18.52-0-virt`。OS64 实际内核 `-O2`，用户两边 `-Os`，整个 46 字节计算函数机器码相同。

| 场景 | OS64 p50 / p99 秒 | Linux p50 / p99 秒 |
|---|---:|---:|
| 单工作者，4.8 亿次 | 0.65 / 0.82 | 0.67 / 0.72 |
| 四工作者，共 4.8 亿次 | 0.67 / 0.93 | 0.71 / 0.80 |
| 十二工作者，共 4.8 亿次 | 0.69 / 0.91 | 0.73 / 0.95 |
| 四工作者，每 65,536 次 yield | 0.75 / 0.98 | 0.91 / 1.77 |
| 32 MiB 管道，逐字节校验 | 0.45 / 0.55 | 0.73 / 1.22 |

单核增加工作者没有并行加速。纯计算的两系统差距较小，存在时钟量化和虚拟化限制。管道该次 p50 为 OS64 71.11 MiB/s、Linux 43.84 MiB/s，约 1.62 倍，只适用于这个含完整校验的单核 TCG/4096 字节管道实验，不能外推到一般系统性能。这里 p99 就是31样本中的最大值，不是调度器响应或生产尾延迟保证。

正式 OS64 样本均通过结果校验和物理页回收检查。独立测试还通过 x87/SSE 初始清零与四线程隔离、8/12 工作者、八进程定时进度、ABI 坏指针拒绝、spawn/exit 日志和 `logsave` 关机冷启动读取。用户数据盘未挂载，前后哈希检查不变。

完整 [测量报告](./measurements/cpu/REPORT.md) 和 [原始 results.json](./measurements/cpu/results.json) 保存实际配置、构建/资产/负载哈希、全部样本和限制。不要只摘表中最快的一格来概括操作系统。
