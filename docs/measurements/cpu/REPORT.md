# Single-vCPU TCG benchmark

QEMU: QEMU emulator version 10.2.0

| Guest | Scenario | Samples | p50 seconds | p99 seconds | p50 million updates/s | p50 MiB/s |
|---|---|---:|---:|---:|---:|---:|
| os64 | single | 31 | 0.65 | 0.82 | 738.46 | 0.00 |
| os64 | four | 31 | 0.67 | 0.93 | 716.42 | 0.00 |
| os64 | twelve | 31 | 0.69 | 0.91 | 695.65 | 0.00 |
| os64 | yield | 31 | 0.75 | 0.98 | 640.00 | 0.00 |
| os64 | pipe | 31 | 0.45 | 0.55 | 0.00 | 71.11 |
| linux | single | 31 | 0.67 | 0.72 | 716.42 | 0.00 |
| linux | four | 31 | 0.71 | 0.80 | 676.06 | 0.00 |
| linux | twelve | 31 | 0.73 | 0.95 | 657.53 | 0.00 |
| linux | yield | 31 | 0.91 | 1.77 | 527.47 | 0.00 |
| linux | pipe | 31 | 0.73 | 1.22 | 0.00 | 43.84 |

CPU scenarios complete 480 million integer updates; pipe transfers and verifies 32 MiB through a 4096-byte pipe. Same source files, both -Os; OS64 kernel -O2.
100 Hz measurement quantization is 10 ms. At 31 samples p99 is the maximum; this is an exploratory tail, not a production p99 estimate.
Single qemu64 vCPU, 128 MiB, TCG, one guest running at a time, ABBA guest order, one warm-up per block/scenario.
OS64 all-thread context switch counts and Linux SELF+reaped CHILDREN rusage counters have different scopes; do not compare them directly.
These are emulator-specific CPU/scheduling measurements, not a claim that OS64 is generally faster than Linux.
Raw samples, binary/asset hashes and exact QEMU arguments: results.json. All temporary guest disks were isolated; original data.img unchanged.

## 本次结果怎样理解

测量日期：2026-10-02。Linux 客体实际内核 6.18.52-0-virt；两边 QEMU 10.2.0 / TCG / qemu64 / 1 vCPU / 128 MiB。OS64 266,040 字节正常内核，实际 -O2；两个用户负载同一编译器 -Os，明确保留 frame pointer；完整 46 字节计算函数机器码一致。原始 JSON 保存配置、哈希和全部 310 个样本。工作目录当时有未提交改动，以 source_sha256 和实际二进制哈希标识快照。

固定总 4.8 亿次计算，OS64 单/四/十二工作者 p50 分别 0.65/0.67/0.69 秒。进程共享同一 CPU，增加工作者没有并行加速。Linux 同三场景为 0.67/0.71/0.73 秒；差异较小，并受 10 ms 量化和虚拟化影响，不能用它宣布现代通用系统整体性能胜负。

每 65,536 次主动 yield 的四进程场景，OS64 p50 从 0.67 增至 0.75 秒，Linux 从 0.71 增至 0.91 秒。本实验测整批任务完成时间，没有测交互响应改善程度。

32 MiB 管道完整逐字节校验场景，OS64 p50 71.11 MiB/s，Linux 43.84 MiB/s，约 1.62 倍。此比例只适用于这次单核 TCG、4096 字节管道、4096 字节读写块、包含创建/阻塞/复制/验证的具体负载，不是一般 IPC、磁盘、网络或真实硬件性能结论。两边管道容量均实际设为4096字节。

每场景31个样本，p99按nearest-rank取第31个，即当次最大值，不能作为可靠的生产尾延迟估计。OS64的所有正式样本物理页数运行前后相同；独立快测覆盖8/12并发完整结果与回收、八工作者定时进度、FPU/SSE隔离、坏指针拒绝和日志关机冷启动保存。用户data.img未被挂载，测试末哈希检查保持不变。

复现入口与解释：[性能教程](../../PERFORMANCE_TUTORIAL.md)。原始记录：[results.json](./results.json)。
