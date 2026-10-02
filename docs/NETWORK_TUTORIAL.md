# 从零写出能与宿主机通信的网络栈

这章的目标是：让 os64 发出真实的 Ethernet 帧，查询下一跳的 MAC 地址，发出和接收 IPv4 包，并让一个用户程序通过 UDP 与宿主机上的程序交换数据。这里的“真实”指字节经过网卡描述符、QEMU 网络后端和宿主机 UDP socket；不是在内核里把输入直接复制给输出。网卡硬件由 QEMU 模拟，协议由我们自己的 C++ 代码实现。

你不需要先懂网络协议。建议先完成 [快速上手](../README.md)，再读 [当前系统教程](BEGINNER_TUTORIAL.md)。读到“物理地址”“用户态”时还不熟悉，可以回看其中的内存与进程部分。本文先做可观察的实验，再解释实验中的每一步。

## 1. 先确认这章实现了什么

| 已实现 | 本章的具体边界 |
|---|---|
| PCI 设备发现 | x86 的传统配置端口；找到一块 legacy virtio-net PCI 网卡 |
| Ethernet 收发 | 接收本机或广播 MAC 的帧；最大 1514 字节，不含 FCS |
| ARP | 查询下一跳 MAC、回复本机 ARP 请求；8 个缓存项，60 秒过期 |
| IPv4 | 固定地址、单接口、同网段直连或默认网关；不处理分片和 IP options |
| ICMP | 发出 ping，接收正确的回包，也回复发给本机的 echo request |
| UDP | 校验和、二进制数据、0 字节包、最大 1200 字节有效载荷 |
| 用户 UDP socket | 4 个全系统槽，每槽 4 个接收包；PID 所有权和过期句柄检查 |
| 回显服务 | 内核占用 UDP 9000 端口，收到什么字节就回复什么字节 |
| 自动实验 | 协议边界测试、真实 QEMU 通信、抓包、吞吐和时延测量 |

没有实现 TCP、HTTP、DNS、DHCP、IPv6、TLS，也没有宣称可以运行浏览器。UDP 接口是本教程的接口，不是 POSIX `socket()` 兼容层。没有网卡时，文件、Shell、进程等本地功能仍然可以运行。

## 2. 先运行三个实验

### 实验 A：从操作系统 ping 虚拟网关

在仓库目录的宿主机终端运行：

```sh
make build
make run
```

启动脚本已经加入 virtio-net 和 QEMU user network。进入 os64 的 Shell 后输入：

```text
net
ping 10.0.2.2
```

`net` 应出现：

```text
network_ready=1
network_driver=virtio-net
network_ip=10.0.2.15
network_gateway=10.0.2.2
network_echo_port=9000
```

`ping` 成功时出现 `ping_sent=1` 和 `ping_received=1`。`ping_rtt_ms` 来自内核 PIT 时钟。如果一次往返发生在同一个 tick 内，显示 0 是时间分辨率的限制，不代表传输没有花时间。

为什么先 ping `10.0.2.2`？它是 QEMU 提供的虚拟网关，可以稳定检验网卡、ARP、IPv4、ICMP 的整条链。QEMU user network 对外部 ICMP 有宿主平台限制，所以不能把“外部网站 ping 失败”直接理解成我们的网卡坏了。默认网络地址和这一限制见 [QEMU 官方网络说明](https://www.qemu.org/docs/master/system/devices/net.html)。

若 `network_ready=0`，先检查是不是用旧的启动命令、没有加 `virtio-net-pci`，或删掉了 `disable-modern=on`。本章驱动操作的是 legacy I/O 接口；纯 modern virtio 接口需要另一套 capability/MMIO 驱动。

### 实验 B：宿主机向 os64 的回显服务发包

保持 os64 开着。在宿主机另一个终端运行：

```sh
python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
    client.settimeout(2)
    message = b'hello from the host\x00binary tail'
    client.sendto(message, ('127.0.0.1', 5555))
    reply, peer = client.recvfrom(65535)
    print('reply:', repr(reply))
    print('all bytes equal:', reply == message)
PY
```

结果应包含 `all bytes equal: True`。中间的 `\x00` 是一个零字节。它验证我们按长度处理网络数据，而不是错误地把数据当成遇到零字节就结束的 C 字符串。

这次通信走的是：

```text
宿主 Python socket
  → 127.0.0.1:5555
  → QEMU hostfwd
  → 网卡 RX queue
  → os64 Ethernet / IPv4 / UDP
  → 内核 9000 端口 echo
  → 网卡 TX queue
  → QEMU
  → 宿主 Python socket
```

`5555` 和 `9000` 为什么不同？5555 是宿主机上的转发入口，9000 是虚拟机里服务监听的端口。`127.0.0.1` 指当前宿主机自己的回环地址；它与虚拟机的 `10.0.2.15` 是不同地址。

### 实验 C：用户程序主动访问宿主服务

本实验反方向通信，还会穿过用户态系统调用。先在宿主终端启动：

```sh
python3 scripts/udp-echo-host.py --port 5151
```

它只打印启动和停止信息，不逐包打印，避免终端输出影响性能。在 os64 中输入：

```text
run /bin/udp_test 10.0.2.2 5151 32
```

成功时包含：

```text
udp_test foreign_handle_rejected
udp_test sent=32 received=32
udp_test binary_zero_max_payload_ok
run_exit_code=0
```

这个程序不仅收一条字符串。它轮流发送 0、1、257、1200 字节的二进制包，检查每个回包的源地址、端口、长度与每个字节。它还启动子进程，证明子进程不能关闭或使用父进程的网络句柄。

如果原有 `build/data.img` 是早期版本，新增用户工具可能尚未进入该盘。先在 os64 中 `shutdown`，再到宿主机运行 `make update-tools`，然后重新 `make run`。更新会保留非工具文件并建立备份；不要为了刷新工具而随意 `make reset-data`，后者会清除数据盘内容。

## 3. 字节、地址、端口分别是什么

网络包是一段字节数组。一个字节通常写成两个十六进制数字，例如 `0x45`。十六进制只是查看同一数值的另一种方式，不会让内存本身发生变化。

本章有三种容易混淆的标识：

| 标识 | 例子 | 回答的问题 |
|---|---|---|
| MAC 地址 | `52:54:00:12:34:56` | 在当前以太网链路上，帧交给哪块网卡？ |
| IPv4 地址 | `10.0.2.15` | 在网络层，把包送给哪个主机？ |
| UDP 端口 | `9000` | 主机收到包以后，把数据交给哪个服务？ |

IP 地址不会自动告诉网卡对方的 MAC。ARP 用来补上这一步。访问同一个 `/24` 网段的地址时，查询该主机；访问别的网段时，查询默认网关，把 Ethernet 帧交给网关，再由网关继续转发。

`/24` 对应掩码 `255.255.255.0`。代码中的判断是：

```cpp
(destination & netmask) == (our_address & netmask)
```

两边相等就认为目的地址在本地网段。当前地址、掩码和网关由 `network_initialize()` 设置，是教学用静态配置；没有运行 DHCP。

协议字段多采用大端顺序，也叫网络字节序。端口 9000 等于十六进制 `0x2328`，包里先放 `0x23`，再放 `0x28`。x86 的普通整数内存布局是小端，所以代码用 `read16`、`write16`、`read32`、`write32` 明确组装字节。把字节数组直接强转成整数指针，会同时带来字节序和未对齐访问问题。

## 4. 先把网卡找到：PCI 配置空间

代码位置：`kernel/device/pci.cpp`。

PCI 设备用 bus、slot、function 三个编号定位。`pci_find_device()` 扫描这些编号，从配置空间读取 vendor/device ID，寻找 `1af4:1000`。这是一块提供旧接口的 virtio 网络设备。

在 x86 传统访问方式里，先往 `0xcf8` 写一个配置地址，再从 `0xcfc` 读相应数据。地址中包含启用位和设备编号：

```text
bit 31       ：启用配置访问
bits 23–16   ：bus
bits 15–11   ：slot
bits 10–8    ：function
bits 7–2     ：寄存器偏移，按 4 字节对齐
```

BAR0 告诉驱动这块设备的 I/O 寄存器从哪个端口开始。驱动验证 BAR0 是 I/O 类型、落在 16 位端口范围，再打开 I/O 和 bus mastering。bus mastering 允许设备进行 DMA。

配置空间的 command/status 相邻。代码只写 command 的 16 位，因为相邻 status 有些位是“写 1 清除”；把整组 32 位读出后原样回写会误清状态。初始化时禁用 MSI-X 与 INTx，等 worker 与 IRQ handler 准备好后才打开传统 INTx 中断。中断接入失败时保留每个 tick 的轮询。可对照 [Linux 的传统 PCI 配置访问实现](https://github.com/torvalds/linux/blob/master/arch/x86/pci/direct.c)，本教程没有复制 Linux 驱动。

## 5. DMA 为什么一定要传物理地址

代码位置：`kernel/net/virtio_net.cpp`。

CPU 访问 C++ 指针时会经过当前 CR3 的页表翻译。网卡进行 DMA 时，描述符中填写的是物理地址。本章不使用 IOMMU，因此不能把一个内核堆虚拟指针直接填给设备。

本章流程是：

```text
alloc_page() → 物理地址
paging_physical_pointer(物理地址) → CPU 可访问的高区指针
描述符.address → 仍填写物理地址
```

为什么 virtqueue ring 要连续物理页？legacy 寄存器只提供 ring 起点的 PFN，即 `physical_address / 4096`。设备按固定偏移找到后面的表，因此整块 ring 必须物理连续。`kmalloc()` 返回的虚拟区域即使连续，也不保证背后的物理页连续。`allocate_ring()` 显式逐页申请、检查连续性；失败会归还已经拿到的页，再尝试下一位置。

这个区别也是上一章 direct map 的实际用途：CPU 可以通过 supervisor 高区访问任意受管理的物理页，不再要求网络缓冲区全部挤在低 2 MiB。

## 6. Virtqueue 的三张表

一个 queue 有三部分：

| 部分 | 谁写 | 作用 |
|---|---|---|
| Descriptor table | 驱动 | 告诉设备缓冲区物理地址、长度、权限和下一项 |
| Available ring | 驱动 | 发布“这些 descriptor chain 可以处理” |
| Used ring | 设备 | 告诉驱动“这些 chain 已处理，写入了多少字节” |

RX queue 是设备向内存写数据；TX queue 是设备从内存读数据。因此 RX descriptor 带 WRITE 位，TX 不带。

初始化顺序是 reset → ACKNOWLEDGE → DRIVER → 选择功能 → 建立两条 queue → DRIVER_OK。这里只协商 MAC 地址功能，不启用校验和 offload、GSO、mergeable RX buffers 或 indirect descriptors。更少的功能让数据路径容易检查，也让所有协议校验由本教程自己的代码完成。

设备给出的 queue size 不能在 legacy 接口里随意改小。本驱动接受 64–256 的 2 的幂，按实际 size 分配 ring。典型 QEMU 给出 256，每条 ring 占 3 个连续的 4 KiB 页。每个包需要两个 descriptor，所以实际 RX/TX 缓冲槽数分别取 `min(64, queue_size / 2)`：queue=256 时各 64 槽，queue=64 时各 32 槽。每个槽占一张 4 KiB 页，QEMU 配置下缓冲页共 512 KiB，再加两条 ring 的 24 KiB。队列仍有明确上限。

发送前 `reap_transmit()` 先回收设备已经用完的 TX 槽，仅检查发送完成，不递归处理接收。即使设备还没有归还前几包，一次预算为 32 包的 RX 回显批次也有足够的 TX 槽。旧版本只有 8 个 TX 槽，实测突发会出现 `tx_busy` 丢包；第 11 节保存了扩大缓冲后的对照数据。

每个缓冲区是一张独立物理页。布局为：

```text
物理页 + 0    ：10 字节 virtio_net_hdr
物理页 + 64   ：Ethernet 帧，最多 1514 字节
```

header 不属于 Ethernet、IP 或应用数据。因为没有协商 ANY_LAYOUT，每个包使用两个 descriptor：第一个专门描述 10 字节 header，第二个描述 Ethernet 帧。这是 legacy framing 的要求，见 [OASIS virtio 规范中的 legacy layout 与 network framing](https://docs.oasis-open.org/virtio/virtio/v1.0/csprd05/virtio-v1.0-csprd05.html)。

### 发布不能只靠“代码看起来有顺序”

先填 payload 与 descriptor，再填 available ring slot，最后递增公开的 available index。设备看到 index 后就可能开始 DMA。`notify()` 在公开 index 前后使用内存屏障，避免设备先看到“有新包”，却还没看到完整描述符。

`next_available` 和 `last_used` 都是 16 位索引，会自然回绕。比较使用 16 位差值，不能在 65535 后错误地判定队列永久为空。读取 used index 后也有读取屏障，再访问设备写好的 used element 与 payload。

读取每个 used element 时，先检查 id 和 length，再访问 payload。id 不属于已发布槽、队列进度超过资源上限等设备错误，会停止网卡收发。页面不会马上释放，因为故障设备可能还在 DMA；把它们还给用户进程反而更危险。这个教学版本通过重启恢复设备，没有实现热重置。

## 7. 一个接收包如何进入用户程序

完整路径是：

```text
网卡 IRQ（只清源并 wake，不解析包）
  → network worker（IRQ 不可用时每 tick 轮询）
  → network_poll(32)
  → virtio_net_poll()
  → 从 used ring 读取 id/length
  → receive_frame()
  → receive_udp()
  → 找到本地端口的 UdpSocket
  → 复制到有界 inbox
  → 用户 udp_receive()
  → int 0x80 / syscall 39
  → 校验用户指针、PID、handle
  → network_udp_receive()
  → 复制 payload 和 metadata 到用户缓冲区
```

worker 使用有界预算，每次最多处理 32 包。IRQ 模式下，已经完成的包还没处理完时先让出 CPU，再继续下一轮，队列为空则阻塞等待；IRQ 不可用时每 tick 检查一次。包解析过程中不会打印串口日志，也不会主动调度；ARP、ping 的等待则会睡眠，让其它可运行进程继续计算。这个批次边界保证持续网络流量不会让一个内核 worker 永远占着 CPU。接收队列始终有界，不能把收件箱扩成无限内存。

驱动把一个 RX 缓冲区交给协议层，协议层在回调返回前处理完或复制有效数据。随后 descriptor chain 会重新发布给网卡。不能在 inbox 中只保存 RX 指针：下一次 DMA 会覆盖同一张页，用户稍后读取的就可能是别的包。

### 线程怎么等网卡，又怎样避免漏唤醒

驱动从 PCI 配置的 `InterruptLine` 读取 BIOS 已配置的 IRQ 路由。PCI INTx 是电平信号，代码把 PC 的 ELCR 对应位设为电平触发；不支持的 IRQ0/1/2/8/13 会让接入失败并回退轮询。只开放 RX 完成中断，TX 完成继续在发送前与 poll 时回收。可以对照 [QEMU 的 PIC/ELCR 实现](https://github.com/qemu/qemu/blob/master/hw/intc/i8259.c)。

`network_handle_irq(line)` 先确认这是网卡所用的 line，再读取 `io_base + 19` 的 ISR 寄存器。读取会清除设备中断源，使 INTx 电平撤销；随后才由共享 IRQ 入口给 PIC 发送 EOI。ISR=0 表示没有本网卡事件，不能把共享 line 上其它设备的中断误认成网卡。实际解析 Ethernet/UDP 的工作留给普通 worker，IRQ 不分配内存、不复制包、不输出串口日志。[virtio 规范](https://docs.oasis-open.org/virtio/virtio/v1.0/csprd05/virtio-v1.0-csprd05.html) 的 4.1.5.5 节描述了 read-to-clear 的用途。

最容易出错的是“检查队列为空”和“登记线程 blocked”之间的时刻。假如包就在此时完成，IRQ 看到线程还在运行，wake 没有效果；线程随后却睡下去，就漏掉了唯一通知。`network_wait_for_event()` 因此先 `cli`，比较 used index 与 last_used，若无包则在保持中断关闭的情况下让调度器登记 blocked，登记完才重新开中断。包先完成会被 index 检查发现；包后完成则会唤醒已登记的线程。

这是单核的等待协议，IRQ 唤醒本身也不承诺立即获得 CPU。当其它用户计算进程占满 CPU 时，网络 worker 仍需等调度器安排；应用的 UDP receive 目前也仍是非阻塞接口。中断主要消除网络空闲时的周期唤醒，并缩短空闲机器上等待下一 tick 的延迟。

## 8. 每层先验长度，再读字段

代码位置：`kernel/net/network.cpp`。

### Ethernet

至少有 14 字节，才能读取目的 MAC、源 MAC、EtherType。只接受本机或广播目的 MAC；源 MAC 必须是合法单播。ARP 与 IPv4 分别进入对应处理函数，其它 EtherType 计入 unsupported。

### ARP

至少有 42 字节完整 Ethernet+ARP。验证 Ethernet 硬件类型、IPv4 协议类型、6/4 地址长度、request/reply 操作码，以及 ARP sender MAC 与 Ethernet source MAC 一致。只对本机 IP 的请求回复。

ARP 缓存不是无限长列表。8 项全满时替换最早学习项；过期则重新查询。ARP 没有身份认证，当前缓存也不是安全防火墙。这章的隔离是进程 socket 的 PID 隔离，不能把它与网络防攻击能力混为一谈。[RFC 826](https://www.rfc-editor.org/rfc/rfc826) 定义了 ARP 字段与解析用途。

### IPv4

先确认版本和头部长度，再检查 total length 是否位于已收到帧内。本章只处理 20 字节基本头；有 options 或分片的包会被明确跳过，不会把半个分片当成完整 UDP 包。

随后验证 TTL、IPv4 头部校验和、目的 IP。协议号 1 交给 ICMP，17 交给 UDP。Ethernet padding 位于 IP total length 之外，不是应用数据。相关头部与长度关系见 [RFC 791](https://www.rfc-editor.org/rfc/rfc791)。

### ICMP

echo 包至少有 8 字节；验证整个 ICMP checksum。回应请求时保留 identifier、sequence 和数据，仅改变 type 与 checksum。ping 还检查回包来源、自己的 identifier/sequence 以及 32 字节测试数据，不能仅看见一个 type=0 包就宣布成功。[RFC 792](https://www.rfc-editor.org/rfc/rfc792) 给出 ICMP echo 格式。

### UDP

UDP 头是 8 字节，包含源端口、目的端口、长度、校验和。长度必须与当前完整 IP payload 一致，接收上限是 1200 字节应用数据。checksum 非零时必须验证，零 checksum 按 IPv4 UDP 规则允许；本实现发送时总会计算 checksum。

checksum 不只是把字节相加：它按 16 位大端字求和，把溢出的高位折回低位，最后取反。奇数长度的最后一个字节放在高 8 位，低 8 位补零。UDP 还把源/目的 IP、协议号和 UDP 长度组成的 pseudo-header 加入计算。计算结果为 0 时发送 `0xffff`，因为 UDP 字段的零值有“不提供 checksum”的含义。见 [RFC 768](https://www.rfc-editor.org/rfc/rfc768)。

## 9. 发送方向与用户接口

发送方向倒过来组装：应用数据 → UDP → IPv4 → Ethernet → virtio descriptor chain。`network_udp_send()` 先解析下一跳 MAC，再组装各层 header 和校验和，最后提交 TX queue。TX 只表示设备接受了发送缓冲区，不代表远端一定收到；UDP 本身不承诺送达、顺序或重传。

用户接口在 `user/udp.hpp`，编号与参数是：

| syscall | 接口 | 作用 |
|---|---|---|
| 36 | `udp_open(port)` | 绑定一个本地 UDP 端口，返回正句柄 |
| 37 | `udp_close(handle)` | 关闭当前进程的 socket |
| 38 | `udp_send(handle, ip, port, data, bytes)` | 发送完整数据报；第五参数从 R8 传入 |
| 39 | `udp_receive(handle, metadata, data, capacity)` | 非阻塞取一个完整数据报 |

返回规则：发送/接收成功返回字节数；`-1` 表示暂时没有包、资源忙或 ARP 超时；`-2` 表示参数或句柄错误；`-3` 表示没有可用网卡。收到零字节 UDP 数据报时返回 0，因此“没有数据”必须使用 `-1`，不能也用 0。

`UdpDatagram` 含源地址、源端口、目的端口、有效载荷长度，共 12 字节（包含尾部对齐字节）。缓冲区太小时返回错误并保留完整包，下次可以换更大的缓冲区重试。一个 socket 的 inbox 满时丢弃新包并增加计数，内核不会无限申请内存。

句柄包含槽编号和 generation。槽关闭后再打开会更换 generation，所以旧句柄不会误操作新的 socket。另一个 PID 即使猜中句柄，也不能 send、receive 或 close；进程退出时内核调用 `network_udp_close_owner(pid)` 回收全部 socket。9000 是内核回显服务的保留端口，用户不能绑定它。

这仍是有界教学 API：没有 `poll/select`、阻塞 receive、TCP stream、FD socket 或多网卡路由。应用目前用 `udp_receive()` 配合 `sleep(1)` 等待。看懂这一层以后，再把 socket 纳入统一文件描述符和等待队列，才不会只是在接口上堆名字。

## 10. 测试为什么分两层

宿主测试运行真实 `network.cpp`，只用假的网卡与时钟提供可控字节。执行：

```sh
bash scripts/test-network-host.sh
```

它用 ASan/UBSan 检查 ARP、ICMP、奇数与最大长度 UDP、错误 checksum、错误 length、分片拒绝、0 字节包、收件箱满、缓冲太小、跨 PID 与旧句柄，以及 10000 个随机 Ethernet 帧。它适合验证字节解析和内存边界，但不能证明 PCI 端口、DMA 或 QEMU 网卡真的工作。

同一脚本还运行 `tests/network_irq_host.cpp`：执行真实 `network_irq.cpp` 的等待协议，用模型替代 CPU 中断标志、网卡寄存器与调度器。它专门模拟“检查为空之后、登记睡眠之后，包才完成”的时序，检查先清设备源再 wake、共享 IRQ 不误认、已有包不睡眠、持续流量批次让出 CPU，以及中断不可用时回退轮询。真实 PCI 路由与电平中断仍由下一层 QEMU 测试验证。

真实系统测试运行：

```sh
make build
bash scripts/test-network.sh
```

它复制模板到临时数据盘，启动真实 virtio-net，运行 ping、宿主往返和用户 UDP 程序，并重复运行故意不关闭 socket 的用户程序。结束时确认用户的原 `build/data.img` 摘要没有改变。

产物在 `build/network-test/`：

| 文件 | 查看什么 |
|---|---|
| `boot-1.serial.log` | Shell 命令、用户测试和内核状态 |
| `boot-1.qemu.log` | 网卡配置错误等 QEMU 信息 |
| `packets.pcap` | 真实通过 QEMU 网络后端的 ARP、ICMP、UDP 帧，可用 Wireshark 查看 |
| `metrics.json` | 本次运行的宿主、QEMU 版本、包数、时延与吞吐实测结果 |

脚本不会每个包打印日志，只在实验结束后汇总。顺序实验要求 128 次完整回显、全字节一致。突发实验发送 256 包，统计实际收到多少；有界 RX/TX 队列允许突发丢包，所以会报告损失，而不会把“允许丢包”伪装成无限带宽。

## 11. 怎样读性能数字

“128 次往返用了多久”与“CPU 每秒处理多少个包”是不同测量。这个脚本记录宿主墙上时间，包含 QEMU、网络 worker、调度和宿主 socket，所以它反映当前整条通信路径，不是纯协议函数的跑分。

`qemu_process_cpu` 另外记录 QEMU 进程从启动到关机的 user+system CPU 秒数，与整次实验墙上时间相除得到单核百分比。此项包含启动、ping、回显、用户 socket 与资源回收测试；不是单独 burst 的 CPU，也没有把宿主 Python echo 服务算入 QEMU。数据中保留每次 RTT、每批收件数和经过时间，避免只留一个平均数。

顺序实验给出：

```text
roundtrips_per_second = 完成往返数 / 宿主经过秒数
 echo_payload_bytes_per_second = 返回的应用数据总字节数 / 经过秒数
```

第二项不是物理链路带宽：没有计入 header，也没有把两个方向都算成一倍额外数据。RTT 给出中位数与第 95 百分位；这些来自宿主高精度计时，与 Shell ping 的 PIT tick 数字不同。

优化时保持 payload、包数、QEMU 参数和宿主环境一致，再比较 `metrics.json`。`network_tx_busy` 或 `network_udp_dropped` 上升意味着队列/应用容量成为瓶颈，未必是 checksum 函数慢。每 tick 轮询增加等待时延与周期唤醒；改成忙轮询会消耗 CPU 并影响其它进程。应同时观察计算工作负载和网络结果，不能只挑一个最高数字。重复实验可以加标签，例如 `bash scripts/test-network.sh --label irq-1 --require-irq`，每轮会保存到独立目录。

### 这次优化的真实对照

以下记录来自 macOS arm64 宿主、QEMU 10.2.0、x86 TCG、128 MiB、1 个虚拟 CPU。内核 `-O2`，用户程序 `-Os`。顺序组每轮 128 次 1200 字节回显；突发组每轮 16 批 × 16 包，每包 1024 字节，每批最长等 150 毫秒。每一行都是完整回归的一轮，不把三轮最佳数字拼在一起。

| 配置 | 轮次 | 突发收件 | 顺序回显/秒 | RTT 中位数 ms | QEMU CPU 秒 | 全实验墙时秒 |
|---|---|---|---|---|---|---|
| RX32/TX8 轮询 | 1 | 232/256 | 94.8 | 9.870 | 1.442 | 8.477 |
| RX32/TX8 轮询 | 2 | 211/256 | 100.3 | 9.681 | 1.555 | 8.387 |
| RX32/TX8 轮询 | 3 | 253/256 | 98.7 | 10.107 | 1.249 | 6.182 |
| RX64/TX64 轮询 | 1 | 256/256 | 97.8 | 9.700 | 1.169 | 6.121 |
| RX64/TX64 轮询 | 2 | 256/256 | 98.6 | 9.999 | 1.261 | 6.121 |
| RX64/TX64 轮询 | 3 | 256/256 | 94.0 | 9.708 | 1.178 | 6.107 |
| RX64/TX64 中断 | 1 | 256/256 | 4119.1 | 0.175 | 1.172 | 4.943 |
| RX64/TX64 中断 | 2 | 256/256 | 3404.9 | 0.192 | 1.126 | 4.686 |
| RX64/TX64 中断 | 3 | 256/256 | 3212.0 | 0.193 | 1.263 | 4.897 |

扩大 TX/RX 槽后，三轮突发均收到全部 256 包，`tx_busy` 均为 0，而顺序 RTT 仍约 10 毫秒。打开中断后，三轮同样全部收件，顺序 RTT 降到约 0.17–0.19 毫秒。这分别支持“减少本组突发的缓冲不足丢包”和“消除空闲机器等待下一 tick”的结论。持续大流量仍有有界队列丢包的可能；满载计算时还需等调度器给 worker CPU 时间。

两种 64 槽构建的用户 ELF 及除 `kernel_main` 宏分支外的内核对象一致。采样脚本增加构建清单记录，使整体源码摘要不同；[对象对照记录](measurements/network/comparison.json) 明确列出了差异。旧 8 槽组是历史基线，期间还调整了启动 BSS 布局与用户参数检查，不能把其整次 CPU 时间变化都归给网络缓冲。64 槽轮询和中断组的 CPU 秒数区间也有重叠，这三轮尚不足以判定 CPU 时间的显著差异。中断组的 CPU 占比约 24–26%，轮询组约 19–21%，其分母是不同的全实验墙时；单看占比会忽略中断组更早完成实验。

[原始数据目录](measurements/network/README.md) 保留每次 RTT、每批收件序号、设备丢包计数、CPU user/system 秒数、构建清单与二进制摘要。PCAP 与串口日志保留在本地对应的 `build/network-poll64/` 和 `build/network-irq64/` 测试目录。该表说明这个实现与配置的效果，不能替代现代操作系统网络栈的统一环境对照，也没有宣称已实现 TCP。

### 自己复现轮询与中断对照

默认 `NETWORK_IRQ_ENABLED=1`。用两个独立构建目录可以防止镜像互相覆盖：

```sh
NETWORK_IRQ_ENABLED=0 BUILD_DIR="$PWD/build/net-poll" bash scripts/build-stage1-image.sh
NETWORK_IRQ_ENABLED=1 BUILD_DIR="$PWD/build/net-irq" bash scripts/build-stage1-image.sh

for round in 1 2 3; do
  BUILD_DIR="$PWD/build/net-poll" bash scripts/test-network.sh --label "poll-$round"
done
for round in 1 2 3; do
  BUILD_DIR="$PWD/build/net-irq" bash scripts/test-network.sh --label "irq-$round" --require-irq
done
```

采样时先完成编译，再运行测试，避免编译占用宿主 CPU。检查 `driver_setup` 中两组都是 RX/TX 64 槽，只有 `network_irq_enabled` 分别为 0/1；中断组应看到有效 IRQ line 和增长的 `network_irq_count`。构建清单的 `network_irq_requested` 记录请求的开关，实际 `net` 输出记录设备是否成功接入；两者不能混用。

下一步适合增加接收预算的调度公平性、统一 FD socket、阻塞接收与 socket 等待队列，再考虑 TCP。每一步都先保留这章的可重复实验，才能判断新功能是否破坏了原先的字节完整性、进程隔离和资源回收。
