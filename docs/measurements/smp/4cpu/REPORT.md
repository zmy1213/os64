# 4-vCPU TCG benchmark

QEMU: QEMU emulator version 10.2.0

| Guest | Scenario | Samples | p50 seconds | p99 seconds | p50 million updates/s | p50 MiB/s |
|---|---|---:|---:|---:|---:|---:|
| os64 | single | 31 | 0.67 | 1.78 | 716.42 | 0.00 |
| os64 | four | 31 | 0.21 | 0.86 | 2285.71 | 0.00 |
| os64 | twelve | 31 | 0.32 | 1.87 | 1500.00 | 0.00 |
| os64 | yield | 31 | 0.35 | 1.04 | 1371.43 | 0.00 |
| os64 | pipe | 31 | 0.34 | 1.00 | 0.00 | 94.12 |
| linux | single | 31 | 0.71 | 1.06 | 676.06 | 0.00 |
| linux | four | 31 | 0.21 | 0.87 | 2285.71 | 0.00 |
| linux | twelve | 31 | 0.21 | 0.39 | 2285.71 | 0.00 |
| linux | yield | 31 | 0.20 | 0.27 | 2400.00 | 0.00 |
| linux | pipe | 31 | 0.53 | 1.37 | 0.00 | 60.38 |

CPU scenarios complete 480 million integer updates; pipe transfers and verifies 32 MiB through a 4096-byte pipe. Same source files, both -Os; OS64 kernel -O2.
100 Hz measurement quantization is 10 ms. At 31 samples p99 is the maximum; this is an exploratory tail, not a production p99 estimate.
4 qemu64 vCPU(s), 128 MiB, TCG, one guest running at a time, ABBA guest order, one warm-up per block/scenario.
OS64 all-thread context switch counts and Linux SELF+reaped CHILDREN rusage counters have different scopes; do not compare them directly.
These are emulator-specific CPU/scheduling measurements, not a claim that OS64 is generally faster than Linux.
Raw samples, binary/asset hashes and exact QEMU arguments: results.json. All temporary guest disks were isolated; original data.img unchanged.

## 宿主等待耗时与实际核心证据

以下 p50/p99 按每客体每场景全部31个 `host_command_seconds` 原始值重新计算，单位秒。客体 tick 表在上方，两个计时范围不相同。

| 客体 | 场景 | 宿主等待 p50 秒 | 宿主等待 p99 秒 |
|---|---|---:|---:|
| os64 | 单工作者 | 0.7509 | 2.3704 |
| os64 | 四工作者 | 0.2741 | 3.7063 |
| os64 | 十二工作者 | 0.4115 | 3.6897 |
| os64 | 四工作者，每65536次yield | 0.5081 | 2.5198 |
| os64 | 32MiB管道完整校验 | 0.4058 | 1.4245 |
| linux | 单工作者 | 0.7313 | 1.1757 |
| linux | 四工作者 | 0.2318 | 0.9017 |
| linux | 十二工作者 | 0.2278 | 0.4282 |
| linux | 四工作者，每65536次yield | 0.2248 | 0.2952 |
| linux | 32MiB管道完整校验 | 0.5485 | 1.3919 |

宿主以 `time.monotonic()` 从命令逐字发送结束后量到下一次 Shell 提示符；排除逐字输入，包含剩余执行、结果串口输出、退出回收、提示符和约10ms轮询。换行发送后先等约5ms，起点可能漏掉最早几毫秒。它不是纯计算耗时，也不是完整键入到响应的端到端延迟。OS64 PIT 是已处理IRQ的软件累计数，IRQ关闭或BKL等待可延迟它；Linux客体monotonic实现也不同。因此保留并列结果，不只用BSP PIT推断绝对速度。

实际在线核心集合均为 `[0, 1, 2, 3]`。以下记录来自每个正式分块前后：

| 分块 | CPU | user_ticks 前 → 后 | user_dispatches 前 → 后（仅OS64） |
|---|---:|---|---|
| os64-0 | 0 | 0 → 1949 | 0 → 17675 |
| os64-0 | 1 | 0 → 3110 | 0 → 13991 |
| os64-0 | 2 | 0 → 1219 | 0 → 341 |
| os64-0 | 3 | 0 → 1177 | 0 → 328 |
| linux-1 | 0 | 0 → 799 | 未使用该指标 |
| linux-1 | 1 | 1 → 1055 | 未使用该指标 |
| linux-1 | 2 | 1 → 618 | 未使用该指标 |
| linux-1 | 3 | 0 → 763 | 未使用该指标 |
| linux-2 | 0 | 0 → 711 | 未使用该指标 |
| linux-2 | 1 | 1 → 698 | 未使用该指标 |
| linux-2 | 2 | 0 → 892 | 未使用该指标 |
| linux-2 | 3 | 2 → 921 | 未使用该指标 |
| os64-3 | 0 | 0 → 3143 | 0 → 15530 |
| os64-3 | 1 | 0 → 3302 | 0 → 11420 |
| os64-3 | 2 | 0 → 1797 | 0 → 752 |
| os64-3 | 3 | 0 → 1773 | 0 → 775 |

OS64计数包含处于Running的用户TCB执行内核服务期间的本地timer计账；Linux值为 `/proc/stat` user+nice jiffies。这里只检查各核心增长，不直接比较两种计数的数值。

全部155个OS64正式样本的物理页数运行前后相同。独立快测覆盖十二工作者x87/SSE隔离、8/12进程计算/回收、八工作者定时进度、32MiB管道、ABI坏指针与logsave冷启动。多核快测还运行smp_test，检查固定绑定、sleep/wake、堆/物理页恢复和guard/NX故障子进程退出。

原始JSON未编辑，SHA256：`5d7eb84abebe7bab21ac5e5221b3bcbcb7d09b16f9e990756ec28090d97f40f0`。完整客体输出保存在 [预检查串口](serial/os64-checks.serial.log)、[日志冷启动串口](serial/os64-log-cold.serial.log)、[OS64首组](serial/os64-0.serial.log)、[Linux首组](serial/linux-1.serial.log)、[Linux次组](serial/linux-2.serial.log)、[OS64末组](serial/os64-3.serial.log)。

三种核数的汇总与测量限制见 [总报告](../REPORT.md)。
