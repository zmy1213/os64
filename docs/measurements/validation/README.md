# 最终集成版本的功能验证

这份记录对应 2026-10-03（Asia/Shanghai）的集成版本。所测 `kernel.bin` 为 **285,776 B**，SHA256 为 `24046ac50ff0829b2b7d99ffbf5465081298d579e41d60154a7ba43b10a16ba3`。构建清单中的源码摘要为 `5562c3b84a422f5a8ef36effb00b40ea0b8ce10c9d1eef2db35eab87b8a767a5`。它包含 APIC 编号和地址检查、就绪队列保序、AP 在线发布、阻塞 sleep 与超时时钟换算修正。

## 验证范围

`make test` 的 18 项全部通过，结束后恢复普通内核。完整输出保存在 [full-suite.log](full-suite.log)。页错误和非法指令两项会刻意构建故障镜像，不能把它们的大小当作普通内核大小；日志末尾重新生成的普通内核才是上面的 285,776 B。故障构建中的未使用变量警告不表示回归失败。

| 检查 | 实际验证内容 | 原始记录 |
|---|---|---|
| 启动、系统与内存 | 自写启动链、用户权限、ELF、动态堆、NX/guard、回收、故障诊断 | [完整回归输出](full-suite.log) |
| 拓扑、调度队列宿主 sanitizer | ACPI/MP 边界与校验、保序轮转、核心归属、时钟域换算、到期和溢出 | [完整回归输出](full-suite.log) |
| 1/2/4 核 SMP | 每组重复 4 次十二工作者计算、私有堆、定时唤醒、故障和退出回收；全部在线核心实际执行；另测浮点、进度、管道和坏指针 | [SMP 结果](smp/results.json) |
| 1/2/4 核协作 | 每组一次 17,829,888 B 长测加四次短测，逐记录、逐字节和计算结果校验；满管道关闭唤醒 EPIPE；物理页和 heap 前后相同 | [协作结果](cooperation/results.json) |
| IPC、Shell、文件与日志 | 解析、继承、原子写、重定向、权限边界、保存和冷启动、I/O 故障 | [完整回归输出](full-suite.log) |
| 1/2/4 核网络 | 真 virtio/IRQ、阻塞 UDP、AP 接收与超时、三计算者混合负载、退出清理 | [最终网络记录](../network-smp/final/README.md) |
| 已安装工具的启动盘副本 | 更新 `/bin` 后，用个人数据盘副本在 3/4 核运行 smp_test、coop_test、fp_test 和 badptr；个人数据盘未被测试改写 | [副本检查结果](installed-disk/results.json) |

SMP 和协作的 `.serial.log` 文件保留原始 CRLF 与终端控制字符，Git 不转换这些字节。每个 QEMU 错误日志均为空。协作的 15 次调用合计校验 **80,234,496 B、36,864 条数据记录**。这些是功能压力实验；各核数的耗时不能直接作为独占环境的性能结论。

## 如何对应二进制

[build-manifest.json](build-manifest.json) 固定普通内核、启动盘和 33 个用户工具的摘要。SMP 结果中的 `kernel_sha256` 是可调试 `kernel.elf` 的摘要，清单中的 `kernel_binary_sha256` 是裸映像 `kernel.bin` 的摘要，两者不能混用。

清单记录 `working_tree_modified=true`：这些测试发生在提交之前，因此 Git 旧版本号不足以识别测试快照。应同时检查源码摘要和二进制摘要。归档前已核对实际二进制，并逐字节复制日志和 JSON；[SHA256SUMS](SHA256SUMS) 保存归档文件摘要。

复现功能验证，在仓库根目录运行：

```sh
make test
for cpus in 1 2 4; do
  bash scripts/test-network.sh --cpus "$cpus" --label "final-smp-$cpus" --require-irq
done
```

测试使用独立数据盘并检查已有 `build/data.img` 摘要，不能与运行中的个人 QEMU 实例并用。正常构建会保留旧数据盘中的用户工具；安装新工具要先退出 QEMU，再运行 `make update-tools`，它保留用户文件并创建备份。
