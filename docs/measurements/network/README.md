# 网络原始测量

实验步骤、数字的含义与结果表见 [网络教程第 11 节](../../NETWORK_TUTORIAL.md)。这里保存原始 JSON，方便重新计算分位数、检查丢包和核对配置。

| 文件 | 实验 |
|---|---|
| `network-test-baseline-1/2/3.json` | 最初 RX32/TX8 的 tick 轮询基线；历史镜像没有完整构建清单 |
| `network-test-poll64-1/2/3.json` | RX64/TX64，`NETWORK_IRQ_ENABLED=0`，最终轮询对照 |
| `network-test-irq64-1/2/3.json` | RX64/TX64，`NETWORK_IRQ_ENABLED=1`，最终中断对照 |
| `build-poll64.json`、`build-irq64.json` | 各组的实际优化参数、编译器、用户 ELF 和启动镜像 SHA256 |
| `comparison.json` | 用户 ELF 与编译后内核对象的一致性检查；不同对象为 `kernel/core/kernel_main.o` |
| `initial-irq/` | 中断刚接入时的额外三轮集成验证；没有用于最终对照表 |

统一配置是 QEMU 10.2.0 的 legacy virtio-net、user NAT、128 MiB、1 vCPU、RX 预算 32、内核 `-O2`、用户 `-Os`。顺序测量 128 次 × 1200 字节；突发测量 16 批 × 16 包 × 1024 字节。各组用户 socket 隔离、进程退出清理与 PCAP 协议检查都通过；最终 64 槽两组还验证了坏指针、超限容量与 64 位参数拒绝。

`raw_rtt_seconds` 保存每次宿主往返时间；`raw_batches` 保存每批收件与经过时间，`received_sequences` 保存所有收到的包号。`guest_counters` 是实验后的真实 Shell 输出，包含设备错误、发送槽忙与 UDP 丢包。

`qemu_process_cpu` 是 QEMU 子进程从启动到关机的完整 CPU user+system 秒数，不是单独网络 burst 的 CPU，也不含宿主 Python echo 服务。百分比除以整次回归墙时；中断组更早完成，不能仅凭更高百分比推断 CPU 开销增加。

最终两组所有用户 ELF 与除 `kernel_main` 宏分支外的内核对象一致。整体 `source_sha256` 包含测试脚本，脚本新增构建清单记录使其不同；保留此差异，没有把不同源码摘要改写成相同。早期 8 槽基线与新版之间还有 BSS 布局及参数检查改动，因此其全实验 CPU 仅供描述，不能用来隔离缓冲扩容的 CPU 成本。
