# 从零启动第二颗 CPU

这篇讲“别的核心怎样开始执行我们的代码”。读完 [主教程](BEGINNER_TUTORIAL.md) 中的启动、页表和用户态章节，再配合 [多核调度教程](SMP_SCHEDULER_TUTORIAL.md) 看运行与等待。代码在 `kernel/cpu/topology.cpp`、`smp.cpp`、`ap_start.asm`、`kernel/interrupts/interrupts.cpp`。

本版是传统 BIOS PC、xAPIC、最多四个 CPU 的教学实现。BSP 处理原来的 PIC/PIT、磁盘、Shell 和网卡工作线程；AP 执行分配给自己的用户进程。用户计算能并行，内核操作先由大内核锁串行保护。启动失败会停止进入交互系统，不能带着半在线核心继续分配进程。

## 1. 先观察结果，不用先背寄存器

在宿主仓库目录执行：

```sh
make build
# 已有 data.img 时，先关闭 QEMU，再装入新用户工具：
make update-tools
OS64_CPUS=4 make run
```

`OS64_CPUS` 是启动 QEMU 的宿主参数。它不是 os64 内部命令；`make run` 默认四核，可指定 1、2、3、4。当前自动回归覆盖 1、2、4 核。

进入 os64 后执行：

```text
smp
run /bin/smp_test
run /bin/coop_test
run /bin/fp_test
smp
```

四核启动应出现 `smp_online_cpus=4`。`smp` 中 `smp_online_mask=15` 表示二进制 `1111`，CPU0–3 都在线；双核为 `3`，单核为 `1`。`apic_id` 是硬件标识，可能不连续；`cpu=0..3` 是内核自己的数组编号。不要混为一谈。

`user_dispatches` 是本核心把用户线程选为运行者的次数，`user_ticks` 是本核心在用户线程处于 Running 时收到的调度时钟归属次数，也包含该线程的内核服务时段。前者证明分到了工作，后者帮助检查用户程序经历了时钟。它们不是精确的硬件 CPU 利用率，也不是全局经过时间。

`smp_test` 会创建十二个计算者并校验完整整数结果、64 KiB 私有堆、定时唤醒、guard/NX 异常退出和回收；程序汇总的 `worker_cpu_mask` 应覆盖全部在线 CPU。`coop_test` 在管道中逐条检查生产者编号、序号、计算结果和每个负载字节，默认传递 17,829,888 字节。看见“启动四核”之后，仍需要这些执行证据。

没有 `/bin/smp_test` 通常是个人数据盘仍保存旧工具。构建会保留它；关闭模拟器后用 `make update-tools` 更新 `/bin`，不要用删除数据的 `reset-data` 当更新步骤。

## 2. 为什么 QEMU 给了四核，内核还要启动它们

“CPU”在这里指 QEMU 暴露的一条可独立执行指令的虚拟处理器。真实机器里它可能对应核心或硬件线程。这不是宿主 Mac 核心数量的直接映射。

机器启动时，固件先让一个处理器进入 BIOS 启动链。它叫 BSP，bootstrap processor。我们的 stage1 → stage2 → 64 位内核首先都在它上面运行。其他处理器叫 AP，application processor，仍等启动消息。

`-smp 4` 只提供四个处理器；它不会替内核选择 AP 的入口、栈和页表。内核需要知道：有哪些 AP、怎样给它发启动消息、它落地时从哪里执行、怎样让它进入我们已经建立的 64 位环境。

## 3. 从固件找处理器名单

固件把机器信息放在内存里的表中。表不是可直接执行的代码，而是长度、地址、校验和、设备编号组成的数据。当前按下面顺序查找：

```text
BIOS/EBDA 的 RSDP
       ↓
RSDT（32 位表地址）或 XSDT（64 位表地址）
       ↓
MADT，签名 APIC
       ↓
启用的 Local APIC 编号 + Local APIC 物理地址
```

EBDA 是 BIOS 使用的扩展数据区；其地址来自 BIOS 数据区 `0x40e`。传统 BIOS 的 RSDP 搜索区域还包括 `0xe0000`–`0x100000`。RSDP 本身指向下一张表，不能把 RSDP 当 CPU 列表。

`firmware_find_cpu_topology` 优先 ACPI；没有有效 ACPI 表时，尝试旧 MultiProcessor 表 `_MP_` → `PCMP`。旧 MP 搜索还包括传统内存最后 1 KiB。支持的是显式 PCMP 配置表，不推测 MP 默认配置，也没有 AML 解释器。

读取每一层前检查长度和可映射范围，之后验证校验和。MADT 子条目也必须满足最小长度、不能越界，启用的 APIC ID 不得重复，名单必须包含 BSP。`0xff` 是物理广播目标，不能作为单颗 AP 的编号；解析与 IPI 发送处都拒绝它，避免损坏的固件名单把逐核 INIT 变成全机广播。初版接收八位 xAPIC ID，保留 BSP 和最多三个 AP；未启用的处理器不加入。超过四个 CPU 的机器仍只使用上限内的核心。

这些检查有实际原因：表指针可能损坏；直接按未经验证的长度读取会访问不存在的内存。解析只在整张候选表通过后才写出结果，不能先公布半张 CPU 名单。

`make test-topology-host` 用真正的解析器处理合成固件内存，并在 ASan/UBSan 下检查 ACPI/MP、XSDT 越界、截断条目、坏校验、重复 ID、关闭 BSP、CPU 上限和失败不发布结果。它验证解析边界，不能代替实际 AP 启动。

## 4. APIC 是消息和时钟设备

8259 PIC 是之前章节的传统外部中断控制器。Local APIC 是每个处理器自己的中断控制器；这里用它发送核心间消息、启动 AP，并给 AP 提供自己的调度时钟。

软件写 Local APIC 寄存器时，访问的是设备，而不是普通内存。物理地址通常为 `0xfee00000`，但代码使用校验后的固件值，并与 `IA32_APIC_BASE` MSR 中的完整地址比较；高位地址不能截断后误当相等。每颗 AP 在写 MMIO 前还验证自己的基址一致且 x2APIC 关闭。MSR 是 CPU 的专用控制寄存器，读写指令为 `rdmsr`/`wrmsr`。

普通页分配器只管理低 256 MiB RAM，不能把 APIC 地址拿去当“可分配的物理页”。`map_device_page` 因此单独允许低 4 GiB 内的设备页，映射到高半区 `0xfffffe0000000000`，只有内核能访问，且不可执行。PWT/PCD 设置为不缓存的设备访问，寄存器用 volatile 读写，写完读 ID 寄存器刷新 posted write。

这里没有切换到完整 IOAPIC 外部 IRQ 路由。BSP 的 LINT0 保留 ExtINT 接收传统 PIC，AP 的 LINT0 被屏蔽。网卡 IRQ11、PIT、键盘、串口仍由 BSP 接收。

| 向量 | 本版用途 |
|---|---|
| `0xf0` | AP Local APIC 周期调度时钟 |
| `0xf1` | 远端就绪/唤醒后的重新调度 IPI |
| `0xff` | APIC spurious interrupt |
| `32`–`47` | 原来的 PIC IRQ |
| `0x80` | 用户系统调用 |

IPI 是 inter-processor interrupt，可以理解成“核心给核心发送的通知”。重新调度 IPI 只要求目标检查就绪工作，不在发送者那里替目标切栈。

## 5. 为什么启动代码放在低地址

AP 收到启动消息时，还不能直接调用已经编译好的 64 位 C++ 函数。它从类似 16 位实模式的起点开始，需要自己的短引导代码。这段代码叫 trampoline，可理解为“跳板”。

| 物理位置 | 内容 | 生存期 |
|---|---|---|
| `0x7000` | AP 实模式 → 保护模式 → long mode 跳板及临时 GDT | AP 启动期间 |
| `0x7e00` | BSP 给当前 AP 的 32 字节邮箱 | 每启动一颗 AP 重写 |
| `0x8000` 起 | 原 stage2 及 BootInfo | 不覆盖 |
| 内核堆分配的 32 KiB | 每颗 AP 的长期 bootstrap 栈 | 该 CPU 在线期间保留 |

低 1 MiB 原本就从物理页分配器中保留，因此跳板和邮箱不会被普通页分配占用。复制跳板发生在启动自测完成后，大小检查限制为 `0x800` 字节；邮箱不放在同一区域内。

邮箱的布局与 `ap_start.asm` 固定偏移匹配：

| 偏移 | 字节数 | 内容 |
|---|---|---|
| 0 | 4 | 根页表物理地址 CR3 |
| 4 | 4 | EFER 中要启用的 LME/NXE 位 |
| 8 | 8 | 本 AP 的 bootstrap 栈顶 |
| 16 | 8 | 64 位 C++ 入口地址 |
| 24 | 4 | 内核 CPU 数组编号 |
| 28 | 4 | 保留 |

C++ 静态断言检查总大小和编号偏移。BSP 写完执行内存屏障，再发送启动消息。一次只启动一个 AP，AP 读完邮箱并报告在线后，BSP 才覆盖为下一颗的内容，因此它们不会抢读不同 AP 的栈。

## 6. INIT、SIPI 到 C++ 的路线

在 `smp_initialize` 中，BSP 给每颗 AP 发 INIT，然后发 SIPI；若尚未报告在线，再补发一次 SIPI。INIT 让 AP 进入规定的启动状态；SIPI 是 startup IPI，携带入口所在的 4 KiB 页编号。本版编号是 `7`，因此起点为 `7 × 4096 = 0x7000`。

短跳板分成三段：

1. **16 位段**：关闭中断，建立段寄存器和临时小栈，装载跳板 GDT，设置 CR0.PE，以远跳转进入 32 位保护模式。
2. **32 位段**：启用 CR4.PAE，装入邮箱中的 CR3，设置 EFER.LME；只有 BSP 已支持并启用 NX 时才设置 NXE。启用 CR0.PG 和 WP，通过远跳转进入 long mode。
3. **64 位段**：装入正式 bootstrap 栈并按 16 字节对齐，从邮箱读取 CPU 编号和函数地址，调用 `smp_ap_entry`。

CR3 是每颗 CPU 自己的寄存器，但初始指向同一个内核根页表。之后每个用户进程有自己的根页表；线程切换保存旧 CR3，再恢复目标 CR3，不共用用户窗口的数据页。

IPI 投递等待有计次上限，AP 在线等待有基于 PIT 的期限；INIT/SIPI 间隔也靠 PIT tick 形成。AP 会检查自己的真实 APIC ID 与邮箱编号对应；错误时停在本 CPU，不宣称在线。BSP 如果收到失败或等不到在线，会停止启动正式交互系统。没有 CPU 热插拔，也没有失败后自动裁减名单继续运行的实现。当前启动间隔、校准和在线期限依赖 BSP PIT 正常送达；若这条虚拟线路本身失效，尚无独立 HPET/TSC watchdog 保证超时返回，因此支持范围仍是已验证的 QEMU 传统 PC。

## 7. 每颗 AP 还需要自己的 TSS、栈和浮点设置

BSP 已经初始化过 GDT/TSS，并不等于 AP 也做好了。GDTR、TR、CR0、CR4、IDTR 都是 CPU 本地状态，AP 必须自己装载。

`initialize_secondary_interrupts` 为 AP 建立自己的 GDT 和 TSS，然后装载 BSP 已建立的共享 IDT。共享的是只读中断门内容；每颗 CPU 的 TSS、默认 RSP0 栈、double-fault IST 栈和 GDT 中的 TSS busy 描述符独立。

用户进程进入内核时，CPU 使用本地 TSS.rsp0。调度器在选择用户线程时，把本地 rsp0 指向该线程的专用内核进入栈。若两颗 CPU 共用一个 TSS，CPU1 的调度会改掉 CPU0 的入口栈，下一次系统调用就可能覆盖另一线程现场。

AP 的 `cpu_initialize_local` 同样启用 x87/SSE，恢复干净的 512 字节 FXSAVE 模板。真正切换用户线程时仍逐线程保存和恢复，不能因为“核不同”就省掉同核上的浮点隔离。AVX/XSAVE 暂未开放。

## 8. 时钟不能因为四核变成四倍速

BSP 的 PIT 是全局 100Hz 计时来源，负责经过时间和睡眠期限。APIC 周期时钟负责 AP 的时间片与本核执行计数，不能把 AP 的每次中断也加到全局 PIT tick 上。

BSP 启动时使用五个 PIT tick 测量 Local APIC 计数下降量，计算约 10ms 的计数值，给各 AP 设置周期模式。这里是简单校准，没有 invariant-TSC deadline timer、CPU 频率治理或高精度 tickless 计时。

睡眠必须释放运行机会。`timer_sleep_ms` 在正式线程中调用 `scheduler_sleep_current_thread`：登记到期 tick，标记 Sleeping，切到其他线程或 idle，后者释放内核锁。到期后 BSP 唤醒线程，并给它固定所属的 CPU 发 IPI。

这与“持锁直接 hlt”等待不同。AP 若持着内核锁等 BSP 的时间，BSP 的时钟入口也要等这把锁，会形成死锁。没有线程的早期单 CPU 启动阶段才保留原来的 HLT 等待。`sleep` 系统调用的参数上限仍为一天，换算和期限加法检查溢出。

## 9. 中断和大内核锁的交界

多核问题不止在 AP 启动。系统调用、中断、用户异常汇编在读调度器或文件表之前先取得 `kernel_gate_enter`。锁由 CPU 所有，不按函数调用递归计数；同 CPU 的内核 IRQ 看到已经持锁即可继续。

锁持有到旧线程 RSP、CR3、FX 都保存完，且目标线程真正恢复。不能在标记下一线程以后、保存旧栈以前放锁。返回 ring3 的汇编在 `iretq` 前保持 IF=0 并放锁；空闲线程则放锁后用一条连续的 `sti; hlt; cli` 等中断，防止就绪通知恰好落在检查与睡眠之间。

APIC/PIC EOI 必须先于可能切栈的调度。EOI 告诉控制器这次中断已经处理到可接下一次的阶段；若切走后很久才发，控制器可能不再给这个 CPU 送后续时钟。spurious APIC 向量例外，不发送 EOI。

更详细的锁、就绪队列、固定核心、阻塞/回收解释见 [多核调度教程](SMP_SCHEDULER_TUTORIAL.md)。

## 10. 验证和常见问题

在宿主执行：

```sh
make test-topology-host
make test-smp
make test-cooperation
# 单独验证多核网络；接收者在 AP，网卡工作线程仍在 BSP。
bash scripts/test-network.sh --cpus 4 --label smp-4 --require-irq
```

这些测试使用临时数据盘，不写个人 `build/data.img`。自动脚本会核对请求的 CPU 数量与实际快照一致、每个核心都有用户工作、完整计算/通信结果正确、物理页和内核堆回到基线。网络关闭与 generation 重用等精确时序另有 sanitizer 模型测试；没有用户多线程/kill API 的场景不冒充用户态实测。

| 现象 | 优先检查 |
|---|---|
| 手动说四核，在线仍只有一核 | 宿主是否设置 OS64_CPUS；固件是否提供有效 MADT/MP；查 dmesg 中的 SMP 警告 |
| `smp startup failed; runtime stopped` | xAPIC 模式、APIC 地址、跳板/邮箱布局、AP 本地初始化；不能跳过错误进入 Shell |
| 在线数正确，但某核计数不增长 | 工作是否足够长且足够多；assigned_cpu、local ready 扫描、AP 周期时钟/IPI |
| 双核在 sleep 后停住 | 是否误回退到持锁 HLT 等全局 PIT；检查正式线程的 Sleeping 路径 |
| 只在第二次运行时出现数据错 | RSP 保存顺序、跨核引用回收、TCB/句柄 generation、浮点现场 |
| 四核不比单核快 | 查看锁内工作比例、进程粒度、固定核心不迁移、TCG/宿主竞争；要正式统计实验 |

性能对照使用 `BENCHMARK_CPUS=2` 或 `4`，与 Linux 使用相同 vCPU/内存/TCG 模式和同一段校验过的计算机器码。方法与数据见 [性能教程](PERFORMANCE_TUTORIAL.md)。本章的启动、计数和正确性回归没有证明本系统是“最快操作系统”。

## 11. 下一步改善什么

初版固定核心是为了保持每个用户地址空间只在一个 CPU 上活跃。它还没有线程迁移、工作窃取、跨核共享地址空间、远程页表失效通知（TLB shootdown）、用户多线程、CPU 热插拔、x2APIC 或 IOAPIC 迁移。大内核锁、共享全局就绪队列和频繁 CPUID 查本核都会限制扩展。

下一步应先用相同负载定位时间花在计算、等待、锁还是复制，再分解内核锁和每核队列。开放迁移/同一用户页表跨核运行之前，必须补齐 TLB shootdown、远端运行状态同步和最后引用回收；简单移走一条线程并不等于安全迁移。

## 12. 原始规范与阅读方式

[ACPI 6.5，系统描述表](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html) 的 RSDP、RSDT/XSDT、MADT 章节定义固件数据。本教程只实现寻找 CPU 所需的子集，未实现完整 ACPI 操作系统层。

[Intel 开发者手册入口](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) 和 [System Programming Guide Volume 3A](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf) 解释 Local APIC、处理器启动、分页和多处理器管理。先用本章追一遍代码，再按寄存器名查规范；不要把网上不同机器的 APIC 常量直接复制进当前布局。
