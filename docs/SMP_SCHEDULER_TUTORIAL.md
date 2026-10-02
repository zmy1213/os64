# 从一个 CPU 到多个 CPU：调度、等待和回收

本章接在 [进程运行教程](PROCESS_RUNTIME.md) 和 [性能教程](PERFORMANCE_TUTORIAL.md) 后面。先把程序在一核下跑通，再读这里。代码入口是 `kernel/cpu/smp.cpp`、`kernel/task/scheduler.cpp`、`kernel/task/context_switch.asm`。这一版最多支持四个 CPU；内核服务串行，用户计算可以并行。

配套 [调度逐函数图解](illustrated/SCHEDULER_FUNCTIONS.md) 画出就绪队列、切栈、睡眠唤醒和回收；[分块归约实验](illustrated/PARALLEL_REDUCTION.md) 把这些机制用到可运行的多进程任务池中。每个函数都能从 [总索引](illustrated/FUNCTION_INDEX.md) 定位。

## 1. 多进程为什么不自动等于多核

把十二个计算程序放到一颗 CPU 上，CPU 只能交替执行它们。每个程序都能前进，但所有计算仍要排队。两个 CPU 则可以在同一个时间段各执行一个程序。这才是这里要验证的并行。

QEMU 的 `-smp 4` 表示给客体操作系统四个虚拟 CPU。它不会替操作系统启动额外的 CPU，也不会替调度器分配工作。如果启动日志说有四个 CPU，却只有 CPU0 的用户执行计数增加，仍然没有完成多核调度。

启动时先工作的 CPU 叫 BSP。其余 CPU 叫 AP。可以先把它们理解成“先到的核心”和“后来启动的核心”。BSP 负责启动 AP、原来的时钟中断和内核工作线程。每个 AP 有自己的内核栈、TSS、局部 APIC 时钟和空闲线程。TSS 的 `rsp0` 决定用户程序进入内核时换到哪根栈；不同 CPU 不能共用一个正在使用的 `rsp0`。

## 2. 哪些状态必须属于每个 CPU

打开 `SchedulerState`。为兼容之前的单核教学检查，CPU0 继续使用 `current_thread`、`idle_thread`、`remaining_slice_ticks` 和 bootstrap 现场。CPU1–3 使用 `secondary_cpus` 中对应的现场。

| 状态 | 含义 | 不能共用的原因 |
|---|---|---|
| current_thread | 这颗 CPU 当前运行谁 | 两颗 CPU 同时执行时答案不同 |
| idle_thread | 没有本地工作时运行谁 | 一个线程只有一根已保存的栈，不能同时在两颗 CPU 上运行 |
| remaining_slice_ticks | 当前时间片剩多久 | CPU1 的时钟不能扣 CPU0 的额度 |
| preempt_requested | 是否需要本地调度 | 一个核心的请求不能意外覆盖另一个核心 |
| bootstrap RSP/FX | AP 启动后最初的恢复现场 | AP 不能退回 BSP 的原始栈 |
| user_dispatches/user_ticks | 此核心调度与记账用户线程的计数 | 用来检查核心是否真的分到工作 |

`user_ticks` 按当前用户 TCB 记账，即使它此刻在执行自己的系统调用或内核服务，也会计入。因此它不是纯 ring3 周期、每进程 CPU 利用率或全局墙上时间；观察计数增长时还要结合工作者的完整计算校验，不能单靠一个数字给出速度结论。

FX 现场是 512 字节、16 字节对齐的 x87/SSE 保存区。每个线程有一份，bootstrap 也有一份。切换时用 `fxsave64` 保存当前线程，再用 `fxrstor64` 恢复下一条线程。只在 BSP 设置 CR0/CR4 不够，AP 也必须打开相同的浮点指令支持并安装干净初始现场。

## 3. 第一步先串行保护内核

多个 CPU 会一起访问进程表、物理页位图、文件描述符和 ready 队列。如果都用“先读，再修改”，会重复取出同一线程或重复分配同一页。

当前版本用一把大内核锁，接口名为 `kernel_gate_enter`。进入系统调用、中断或异常时取得它。执行内核中的后续步骤时一直持有；同一 CPU 的嵌套入口看到锁已由自己持有，不再递归增加次数。准备返回用户态时，`kernel_gate_leave_user` 关闭本地中断并释放锁，随后由 `iretq` 恢复用户的中断标志。

因此两颗 CPU 的用户计算可以同时运行，但两颗 CPU 不能同时修改文件系统。这是可验证的第一步。它还没有细粒度锁、每核内存分配器或并行文件系统；大量短系统调用可能被这把锁限制。

“持有全局锁”和“关闭本地中断”解决不同问题。全局锁防止别的 CPU 进入；关闭中断防止本 CPU 在一半更新时被 IRQ 打断。调度器用 `LocalIrqGuard` 记住原来的 IF，关中断完成切换，再在这条内核执行路径真正恢复后还原 IF。

## 4. 为什么切换前不能先开中断

看 `switch_thread_context` 的顺序：

1. 关闭本地中断，保存当前线程依赖的 CR3。
2. 选择并标记下一条线程，更新本 CPU 的 TSS 和 current 指针。
3. 汇编保存旧线程 FX、寄存器和 RSP，切换 CR3，再装入目标栈。
4. 目标执行路径继续运行。它返回用户时释放锁，或者在空闲循环中释放锁。

第 2 和第 3 步之间，current 指针已经是新线程，CPU 实际还在旧栈。如果此时提前 `sti`，中断可能把旧栈上的现场误认成新线程。更坏的情况是唤醒旧线程，让另一颗 CPU 恢复尚未保存好的 RSP。

所以 `scheduler_block_current_thread_and_enable_interrupts` 的含义是：保存和切换全过程保持 IRQ 关闭；等原线程被唤醒、恢复到这次调用后，才重新启用中断。目标线程恢复自己的 IF，不需要旧线程提前开中断替它“救活时钟”。

`user_mode_enter` 的第一次 ring3 入口也遵守同一顺序：先构好 SS/RSP/RFLAGS/CS/RIP 五项返回帧，再释放内核锁，最后 `iretq`。`user_mode_resume_kernel` 是用户退出后接回内核的路径，继续持锁以完成退出与调度。

## 5. 如何把工作分到核心

当前仍使用全局固定容量的优先级 ready 队列。`queued` 表示 TCB 已在队列中，防止重复入队。同优先级采用轮转。

用户线程首次被取出时，调度器选择已分配活用户线程最少的在线 CPU，把编号写到 `assigned_cpu`。之后不迁移。内核工作线程只能在 CPU0 执行。

一颗 CPU 扫描队列时，只取属于自己的第一个线程。每次完整扫描原来的条目数，其余条目按原顺序放回，保留各核心的 FIFO。比如 `[A1, B0, C1]` 中 CPU0 取出 B0 后，剩下应为 `[A1, C1]`；不能只把 A1 转到后面留下 `[C1, A1]`，否则一个核心会改变另一个核心的轮转顺序。扫描长度有上限，因此不会因为“队列有东西，但都属于别的核心”而永远占着锁。新工作或唤醒会给目标核心发送重新调度 IPI。IPI 可以理解成核心之间的“请检查工作队列”通知。

固定绑定使本次实现容易确认地址空间和栈的生命周期。它也意味着没有负载迁移：某核心上的长任务结束后，不能自动替另一颗仍繁忙的核心分担已经绑定的任务。真实通用操作系统通常会继续实现迁移、每核队列、工作窃取或其他均衡机制。

## 6. 空闲不能一直持锁

`idle_thread_entry` 先在锁内检查 ready 工作并回收已退出的孤儿进程。没有工作时，释放锁，然后连续执行：

```asm
sti
hlt
cli
```

`sti` 后 CPU 有一个很短的中断延迟窗口，下一条 `hlt` 与它配合，避免出现“先收到唤醒通知，随后才睡下去，再也没有通知”的丢失唤醒。醒来后重新取得内核锁，再查看共享队列。

AP 无工作时必须回到自己的 idle 线程。即使所有用户程序都退出了，AP 也不能使用 BSP 的 bootstrap RSP 返回到 `kernel_main`。

## 7. 全局时间和局部时间片

BSP 原有 100 Hz PIT 的 `timer_tick_count()` 是启动以来的绝对时间；`SchedulerState.total_ticks` 是本次调度器初始化以来的相对 tick，只有 BSP 增加。`sleep(1)` 是等待至少一次这样的全局 tick，不是“任意一颗 CPU 走了一次时钟”。四核不能让全局时间变成四倍快。

BSP 的 `scheduler_handle_timer_tick` 增加全局 tick、扫描等待期限，再进行 CPU0 的局部计账。AP 每约 10 ms 的局部 APIC 时钟只调用 `scheduler_handle_local_timer_tick`，扣本 CPU 的时间片、增加本 CPU 统计。

`scheduler_block_current_thread_until(deadline)` 的接口使用绝对 PIT tick；内部按 `deadline - timer_tick_count() + scheduler.total_ticks` 转成调度器的相对 wake_tick，不能直接比较两个起点不同的计数器。`0` 或 `UINT64_MAX` 表示无限等待；有限 deadline 已过则立即返回 false。唤醒会清除 deadline，调整 blocked/sleeping 计数，再把线程放回其绑定核心的队列。BSP 唤醒 AP 上的线程后，IPI 提醒该 AP 调度。网络读者醒来还要重新检查“是否真有数据、是否关闭、是否超时”，因为唤醒本身并不等于读到了数据。

`sleep` 的系统调用也必须登记线程睡眠。不能让 AP 持有内核锁、直接 `hlt` 等 BSP 的 tick：AP 在等时间，BSP 的时钟 IRQ 在等 AP 释放锁，两边会永久互等。本次双核压力测试正是用共同开始期限和反复 sleep 暴露了这个问题；正式线程的毫秒睡眠经 `scheduler_sleep_current_thread` 切换，启动阶段无线程的计时才继续用原来的 HLT。

## 8. 退出以后何时才允许释放栈

线程退出会关闭它的文件描述符和 UDP 所有权、唤醒等待者，把状态改成 finished/exited。此时它还可能站在自己的栈上执行最后几条切换指令，不能立即 `kfree` 这根栈。

`process_is_current` 扫描所有 CPU 的 current 指针。`reap_orphans`、`scheduler_discard_process`、`scheduler_destroy` 都用这个检查；只有进程不再属于任何核心的正在运行现场，才能回收。锁一直覆盖“更新 current → 保存旧 RSP → 切到新栈”的过程，所以别的 CPU 不会在这段窗口中回收旧栈。

每个用户进程当前只有一条用户线程和私有用户页表。固定绑定也避免了跨 CPU 迁移带来的 TLB 失效通知问题。不能据此宣称已经有完整的共享地址空间多线程、跨 CPU TLB shootdown 或 `fork`。

## 9. 如何验证，而不是只看 CPU 数量

在仓库根目录：

```sh
make build
make test-scheduler-host
make test-smp
```

宿主队列测试直接编译 `tests/scheduler_queue_host.cpp` 引入的正式 `scheduler.cpp`，不是另写一个相似调度器。它只替换 CPU 编号、在线数、IRQ 标志、PIT tick 和 IPI 通知，用一个简单数组作为顺序答案，验证真实取队 helper：跨核心 FIFO、全是其他核心任务时有限返回、重复入队拒绝、优先级、第一次分配后固定绑定、2000 轮环形头尾回绕、容量边界、绝对/相对时钟零点、过期/无限/溢出期限，以及 AP 时钟不会增加全局时间。ASan/UBSan 检查宿主内存行为。这个测试不执行真实 CR3、CLI、IPI 或汇编切栈，不能代替 QEMU 测试。

客体测试使用自己的临时数据盘。它分别启动 1、2、4 个虚拟 CPU，每种配置反复运行十二进程压力程序。`run /bin/smp_test` 的成功标志是：

```text
smp_test online_cpus=4
smp_test worker_cpu_mask=15
smp_test twelve_workers_pin_compute_wake_resources_ok
run_exit_code=0
```

mask 是位集合。十进制 15 等于二进制 `1111`，表示 CPU0–3 都报告过用户进程执行；两核预期 3，一核预期 1。只看 mask 还不够：程序也要求各核心的用户调度与计时统计增加，校验每个 worker 的完整 64 位结果，检查跨 sleep 的 CPU 编号不变。

每个 worker 六轮计算，每轮先分配 64 KiB，写入与 worker/轮次/偏移有关的字节，计算后逐字节检查并释放。结果以短原子管道记录送回父进程。之后重复启动 stackfault/nxfault 子进程，要求它们单独以 142 退出，再检查空闲物理页和内核堆用量回到起点。另跑十二 worker 的 SSE/x87 检查、旧 IPC、无效指针、计算与管道校验，验证旧功能在 SMP 下仍能使用。

失败时先看隔离构建目录的 `smp-test-results/*.serial.log`。`online_cpus` 不符先查 AP 启动；mask 不全查队列绑定/空闲唤醒；full64 校验失败查并发运行同一 TCB 或寄存器保存；资源不回收查当前栈检查、wait/reap 和 fd 引用；时钟不动查 BSP PIT 与 AP 局部时钟有没有混淆。

## 10. 性能实验的条件

先停止其他 QEMU 和大规模构建，再运行相同配置：

```sh
BENCHMARK_CPUS=1 make benchmark
BENCHMARK_CPUS=2 make benchmark
BENCHMARK_CPUS=4 make benchmark
```

Linux 资产准备步骤见 [性能教程](PERFORMANCE_TUTORIAL.md)。每一轮 OS64 与 Linux 使用相同 vCPU 数、128 MiB、qemu64 和 TCG，用户计算源码、编译器及整个计算函数的机器码一致。多核使用 `tcg,thread=multi`。每个整数计算场景总量保持 4.8 亿次更新；pipe 场景另用 32 MiB 传输并逐字节校验。不能把“四 worker 各做一份”与“一 worker 只做四分之一”混成加速比。

一核数据输出到 `performance-results`；二核、四核分别输出到 `performance-results-2cpu`、`performance-results-4cpu`。单 worker 并不能利用四核执行单条循环；四/十二 worker 才能观察用户计算并行。频繁 yield 和 pipe 则同时测到内核锁、调度、阻塞与完整内容校验的成本。

报告应同时列出正确性、固定工作量的 p50/p99、每核执行证据和资源回收。计时仍是 100 Hz，短于几个 tick 的差别不可信；31 样本的 p99 等于最大值。TCG 多线程速度还受宿主核心数、调度和负载影响，不能把虚拟机测量宣称为真实硬件普遍结论。

还要并列宿主 `host_command_seconds` 的 p50/p99。它从命令发送结束后量到下一次提示符，排除逐字键入，包含剩余执行、输出和宿主轮询；不是纯计算时间。OS64 的 BSP PIT 软件 tick 可能因 IRQ 关闭或内核锁等待延迟，Linux 的客体 monotonic clock 实现也不同。只有客体 tick 变少、宿主等待却没有改善时，不能把结果写成真实加速。精确计时范围见 [性能教程](PERFORMANCE_TUTORIAL.md#8-耗时与-p50--p99-怎么读)。

## 11. 本次实测结果

[同一新镜像的1/2/4核正式报告](measurements/smp/REPORT.md) 保存两客体各五场景、每场景31次，共930个正式样本，以及每核实际执行证据。按宿主等待p50，OS64四工作者从一核0.6991秒降到四核0.2741秒，约2.55倍；十二工作者从0.7425降到0.4115秒，约1.80倍。

四核十二工作者Linux约0.2278秒，OS64仍较慢；OS64频繁yield四核约0.5081秒，反而慢于双核0.4577秒。当前内核串行、线程固定绑定是明确限制，但实验尚未测锁等待/持有与装载/创建阶段时间，不能直接认定每种因素的贡献。先补观测，再优化；本次数据没有证明最快或线性扩展。

所有长样本保留，报告并列客体/宿主p50和p99以及 [原始记录摘要](measurements/smp/SHA256SUMS)。计时范围、31样本最大值和TCG限制仍适用。
