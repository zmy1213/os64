# UDP 阻塞接收与多核计算的功能实验

采样时间：2026-10-03 Asia/Shanghai（构建清单 UTC 为 2026-10-02）。宿主 macOS arm64，QEMU 10.2.0，qemu64，x86 TCG，128 MiB，内核 -O2、用户 -Os；2/4 vCPU 使用多线程 TCG。网卡 legacy virtio-net，RX/TX 各 64 槽，IRQ11，单队列 BSP worker。三个组使用同一 disk.img 和同一用户 ELF。

这是每组一次的功能回归，采样期间其它代理也可能运行独立系统测试，不是独占宿主的统计性能比较。不能把这里的耗时当成可靠加速比，也不能与上一轮只测试网络的 CPU 秒数直接比较。

后续审计发现 PIT 与调度器计数的零点不同，有限等待截止值需转换时钟域。本目录旧测试只验证 25ms 调用最终超时且经过至少 3tick，没有检查上界，也没有让这个超时场景在 AP 上运行。因此这些 JSON 不证明有限超时时限正确。最新测试要求 AP 上 25ms 等待在空闲环境的 3–6tick 内返回；新版镜像需使用最新脚本重跑验证，旧数据保持原样。64/64 字节回显、AP 接收、计算校验和与资源回收的旧证据仍按原记录保留。

最终镜像已按最新脚本重跑：1/2/4 核均在 3tick 返回，2/4 核等待者在 AP1。原始新结果与镜像摘要另存于 [最终网络回归](final/README.md)。

每组完整回归包括：ARP/ICMP、128 次宿主顺序回显、256 包突发、原非阻塞用户 API 36–39、syscall40 坏参数/跨 PID、25ms timeout、0 与 3 个计算者两组阻塞 UDP 测试，以及 8 次退出自动关闭 socket。用户数据盘摘要不变，物理页无泄漏；设备错误、UDP dropped 与 tx_busy 都为 0。

| vCPU | 计算子进程 | 完整 UDP 往返 | 接收 CPU | 每 CPU 用户 tick 增量 | 整组用户 elapsed_ticks | 宿主整条 Shell 命令秒数 |
|---|---|---|---|---|---|---|
| 1 | 0 | 64/64 | 0 | [4, 0, 0, 0] | 9 | 0.491 |
| 1 | 3 | 64/64 | 0 | [52, 0, 0, 0] | 55 | 1.033 |
| 2 | 0 | 64/64 | 1 | [2, 1, 0, 0] | 5 | 0.403 |
| 2 | 3 | 64/64 | 1 | [30, 15, 0, 0] | 32 | 0.672 |
| 4 | 0 | 64/64 | 1 | [1, 2, 0, 0] | 4 | 0.452 |
| 4 | 3 | 64/64 | 1 | [21, 2, 17, 16] | 23 | 0.586 |

3 个计算者每个运行一亿次确定整数运算，完整 64 位结果先由 bench 自校验，父进程再验证每个退出 checksum，三者合计 283。接收子进程通过继承 pipe 等父进程创建计算者后再开始收发；它逐字节检查 0/1/257/1200B，检查小输出缓冲保留包、NULL+0 接收零字节包、有限与无限等待。2/4 核组接收进程在 AP1，网卡 ring 仍只由 BSP 消费。

`elapsed_ticks` 以 BSP 全局 100Hz 时钟计时，覆盖创建、收发和 waitpid，不是纯网络 RTT；各 CPU `user_ticks` 是本核用户线程处于 Running 时的 timer 归属计数（也包含该线程的内核服务时段），没有加入 BSP 全局时钟。`qemu_process_cpu` 覆盖从启动到关机的完整回归，包含计算实验。原始 `raw_output` 保留所有校验和 CPU 字段，PCAP/串口完整输出在本地 `/tmp/os64-network-smp/network-test-smp-{1,2,4}`。

目录中的 JSON 是脚本原始输出（早期字段解析另外保留了无关的 `guest_fields.cpu=3`，实际 CPU 数据请使用 `cpu_progress` 与 `receiving_cpu`）。`build-manifest.json` 固定所测镜像、内核及用户 ELF 摘要。之后脚本只修正了这个多余字段的解析，文档也继续补充，因此当前源码摘要与构建清单可不同；不会据此声称已测试所有后续源码。

复现最新功能测试：

```sh
BUILD_DIR="$PWD/build/net-smp" bash scripts/build-stage1-image.sh
for cpus in 1 2 4; do
  BUILD_DIR="$PWD/build/net-smp" bash scripts/test-network.sh --cpus "$cpus" --label "smp-$cpus" --require-irq
done
```

关闭/退出时唤醒、槽 generation 重用、等待后用户映射失效和设备故障的精确时序由 `tests/network_host.cpp` sanitizer 模型覆盖。本次 QEMU 没有同进程用户线程或 kill ABI，所以不把这些模型场景冒充真实用户线程测试。
