# SMP 启动三张图的源码核对规格

这三张图用于 [SMP_BOOT_FUNCTIONS.md](../SMP_BOOT_FUNCTIONS.md)。图是当前源码的教学解释，不是未来架构。统一采用用户参考图的白底、三列两行、(a)–(f) 六栏、淡蓝/淡绿/淡橙/淡紫底色、细边框、粗黑标题、小流程箭头、编号步骤与底部一句话总结；中文大字，函数名保持原拼写。每栏放少量文字，完整函数解释放 Markdown。请直接使用 imagegen 生成，不用绘图代码重画。

## 1. images/smp-boot.png

标题：**OS64 多核启动与私有现场**。副标题：**先准备每颗 CPU，再允许用户计算并行**。

- **(a) BSP 总指挥**：CPU0/BSP → 固件拓扑 → LAPIC 映射 → 调度器/私有栈 → AP 逐颗启动。标 `smp_initialize()`、最多 4 核。底句：没有有效拓扑时仅 BSP；多核启动中途失败则停止正式运行。
- **(b) 低地址启动桥**：0x7000「启动代码」与 0x7e00「32 字节邮箱」分开；邮箱字段 root / efer / stack / entry / index。BSP 箭头向 AP：INIT assert → 1 PIT tick → INIT deassert + SIPI → 1 PIT tick → 必要时第二次 SIPI。标 `send_ipi()`。底句：一次只启动一颗 AP，确认 online 后才复用邮箱。
- **(c) 16 → 32 → 64 位**：三段阶梯 16 位实模式 / 32 位保护模式 / 64 位长模式；关键短词 GDT → CR0.PE；CR4.PAE → CR3 → EFER.LME/NXE → CR0.PG/WP；独立 RSP → `smp_ap_entry(index)`。底句：SIPI 从物理 0x7000 开始执行。
- **(d) 每核独立中断现场**：CPU0 与 CPU1 两个盒，各自 GDT、TSS、RSP0、IST1；两者箭头指向同一个「共享只读 IDT」。标 `initialize_tss()` / `initialize_secondary_interrupts()`。底句：用户进内核用 RSP0；双重故障用 IST1。
- **(e) 浮点现场不串台**：干净 512B 模板 → CPU0、CPU1 与线程的 FXSAVE 镜像；示意旧线程保存、新线程恢复。标 `cpu_initialize()` / `cpu_initialize_local()`。底句：x87 + SSE 已支持；AVX/XSAVE 尚未支持。
- **(f) online 发布顺序**：AP「完成私有初始化」→「online_count + 1」→「release: online=1」→「等待内核锁」；BSP「acquire 读 online」→「下一颗 AP」。标 `smp_ap_entry()`。底句：online 表示启动准备完成，不表示已经执行用户程序。

不可误画：AP 不是再次执行 BIOS；BSP 启动循环持内核锁，AP 先发布 online 再等锁；不要写 AP 发布后 BSP 才加总数；不要画所有 AP 并发读取同一个被覆盖的邮箱。

## 2. images/kernel-gate.png

标题：**OS64 多核并行与内核锁**。副标题：**用户计算可并行 · 共享内核状态串行**。

- **(a) 四核用户计算**：CPU0–CPU3 各自绿色用户计算轨道，同时运行；下方只有一个蓝色「共享内核服务区」。底句：已实现固定 CPU 归属；尚未实现迁移。
- **(b) CPU 所有的大锁**：owner = CPU1，CPU1 可访问调度器/页表/文件/网络状态，CPU0 与 CPU2 在门外 `pause`。标 `kernel_gate_enter()`。底句：锁属于 CPU，切线程/内核栈时仍持有；不是递归计数锁。
- **(c) 入内核与 IF**：IRQ / syscall → 保存现场 → 保存 IF 并 CLI → acquire/CAS 取得锁 → 恢复原 IF（原来为 1 才 STI）→ C++。底句：等锁时 IF=0，避免本核半入锁时嵌套 IRQ。
- **(d) 放锁的两个出口**：用户返回：CLI → release → 恢复现场 → IRETQ；空闲等待：CLI → release → STI; HLT; CLI → acquire。标 `kernel_gate_leave_user()` / `kernel_gate_release_idle()`。底句：返回内核继续执行时保持锁；不能持锁在 AP 等 BSP 时钟。
- **(e) 一份时间，多核记账**：左「BSP：PIT IRQ0 → 全局 tick +1 + CPU0 记账」；右「AP：LAPIC 0xF0 → 本核记账」。禁止右边箭头指向全局 tick。底句：四核不会让系统时间变快四倍；user_ticks 包含该用户线程的内核服务时间。
- **(f) 跨核唤醒与 EOI**：CPU0 新任务/wake → 目标 CPU1 的调度请求 → IPI 0xF1 → EOI → 检查是否让出 CPU；小框「0xFF 伪中断：无 EOI」。标 `smp_send_reschedule()` / `kernel_handle_irq()`。底句：EOI 要在可能切换栈之前完成；IPI 本身不增加全局时间。

不可误画：同核内核 IRQ 进入锁没有递归深度；用户返回放锁与普通内核 IRQ 返回放锁不同；AP 的时钟不递增全局 PIT；伪中断不写 EOI；当前大锁没有公平排队、不是已经实现的工作窃取或无锁调度。

## 3. images/topology.png

标题：**OS64 如何可靠发现 CPU**。副标题：**固件字节 → 校验结构 → CPU 清单 → xAPIC 启动**。

- **(a) 从哪里寻找**：EBDA 前 1 KiB 与 BIOS 0xE0000–0x100000；ACPI 先查、MP 后备；MP 再查常规内存最后 1 KiB。标 `firmware_find_cpu_topology()` / `scan()`。底句：每 16 字节试一个候选，读取必须在有效物理范围内。
- **(b) ACPI 指针链**：RSDP → RSDT(32 位指针) 或 XSDT(64 位指针) → APIC/MADT。标 `acpi()` / `sdt()`。底句：签名、长度、字节和校验通过，才继续追指针。
- **(c) MADT 枚举**：表头后逐条记录 → Type 0「启用的 Local APIC」→ 去重 → BSP 放索引 0 → 最多 4 个；Type 5「LAPIC 地址覆盖」。标 `madt()` / `add_cpu()`。底句：禁用 CPU 不加入；重复 ID 或缺 BSP 拒绝整张表。
- **(d) MP 后备**：_MP_ 浮动指针 → PCMP 配置表 → 20B CPU 条目 / 8B 其他条目 → 校验 → CPU 清单。标 `mp()`。底句：ACPI 未给出有效结果才尝试 MP；不支持默认配置表。
- **(e) 字节不是对象**：小端例子「34 12 → 0x1234」；sig「APIC」；sum「全部字节相加 mod 256 = 0」。标 `u16/u32/u64`、`signature()`、`checksum()`。底句：先验边界再解析，避免越界或误认表。
- **(f) xAPIC 安全闸**：绿框「ID 0..254」「LAPIC 地址非零、4KiB 对齐、低于 4GiB」「MSR 完整地址匹配」；红框「255 是广播」「x2APIC 当前拒绝」。标 `xapic_unicast_id_valid()` / `xapic_base_supported()`。底句：解析成功后才一次性发布 CpuTopology；无有效表可回退单核。

不可误画：BSP ID 不必是硬件 ID 0，内部 CPU 索引 0 才固定是 BSP；超过 4 核是能力截取，不是启动超过 4 核；x2APIC 条目不属于当前 Type 0 支持路径；检查所有启用 CPU 的重复 ID，即使容量已满也不跳过校验。
