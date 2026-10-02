# 调度器信息图内容规格

用于直接调用图像生成工具。这个文件记录内容设计与图块映射；生成后的实际提示词、最终图和审校记录分别保存在 `../prompts/`、`../images/`、`../images-scheduler.json`（ready 图由根代理另外登记）。所有内容对照提交 `67efd0d008e0b58b31c89ad4ecfbab502416e087` 的 `kernel/task/scheduler.cpp`、`scheduler.hpp`、`context_switch.asm`。

统一画风：参照用户给出的 Redis Set 六栏信息图，白底、黑色宋体/衬线大标题、三列两行六个圆角边框面板，面板依次标 `(a)` 到 `(f)`。蓝色表示 ready/输入，绿色表示被选中/成功，橙色表示等待/约束，红色表示不能破坏的不变量。淡色底、细线箭头、小表格、编号步骤；用直观的 CPU 小方块、队列格子、栈格子、时间轴，不画装饰性芯片照片。横向约 3:2，优先保证中文清晰；正文不堆全部函数名，长函数名在 Markdown 中查。每个图块都必须写清“当前实现”，不写最快/无锁/自动迁移。

## 1. `images/ready-queue.png`

实际标题：**多核就绪队列：怎样取出本核任务**。
实际副标题：**当前实现 · 固定核心 · 完整扫描 · 保持各核 FIFO**。

| 图块 | 图面内容 | 对应函数 |
| --- | --- | --- |
| (a) 谁在本核运行 | CPU0 正在检查 `[A1,B0,C1]`，只有 B0 匹配；普通内核线程只 CPU0，用户检查 assigned_cpu，未绑定时选轻核。 | `thread_eligible`、`has_local_ready` |
| (b) 首次选 CPU | CPU0/1/2/3 当前绑定负载 `[2,1,3,1]` → 首选 CPU1；同负载选编号较小。首次运行写入 assigned_cpu，之后固定。负载包括睡眠/阻塞的活用户线程。 | `least_loaded_user_cpu` |
| (c) 取一个槽 | 环形队列读 head 槽位→head 前移→ready_count 减一→queued=false；槽位号定位 TCB，原始 pop 尚未检查 CPU 归属。 | `pop_raw_ready_thread` |
| (d) 三步保序扫描 | ①弹 A1，不匹配，放尾；②弹 B0，匹配，留在手里；③弹 C1，放尾。结果 `[A1,C1]`，不是 `[C1,A1]`。只扫描开始时的 3 个元素，重入队不发送 IPI。 | `pop_ready_thread_from_priority` |
| (e) 入队与通知 | 检查 Ready/未 queued/容量→槽位放尾→queued=true→需要时通知目标 CPU。普通新任务/唤醒通知；轮转放回不发 IPI。 | `push_ready_thread`、`notify_ready_cpu`、`thread_slot_index`、`is_runnable_priority` |
| (f) 复杂度和边界 | 小表：入队槽位反查 O(T)；一轮选择最坏 O(T²)；T≤32；用户并行，内核 BKL 串行。固定绑定没有 work stealing；高优先级持续 ready 可能让低优先级等待。 | `pop_highest_ready_thread`、`select_next_runnable_thread` |

底部记忆句：**先找本核能跑的线程；完整扫描，保留别人次序。**

## 2. `images/context-switch.png`

标题：**上下文切换：把线程的“继续位置”交接好**。
副标题：**持有内核锁 · 关闭本地中断 · 保存 RSP / CR3 / 浮点现场**。

| 图块 | 图面内容 | 对应函数 |
| --- | --- | --- |
| (a) 每核与每线程 | 每 CPU 方块：current、slice、preempt、idle；每 TCB 方块：saved RSP、saved CR3、FX state。四 CPU 共享 ready 队列，current 指针各自独立。 | `local_*`、`scheduler_cpu_current_thread` |
| (b) 新线程初始栈 | 栈从高到低画 bootstrap 返回地址、RFLAGS=0x2（IF=0）、rbp/rbx/r12-r15。恢复六寄存器 → popfq → ret → bootstrap。 | `prepare_initial_thread_stack`、`scheduler_thread_bootstrap` |
| (c) 切换顺序 | ①持 BKL + CLI；②记旧 CR3；③准备新 current / 本核 TSS.RSP0；④保存 FX 与旧 RSP；⑤加载新 CR3；⑥加载新 RSP、恢复并 ret。红色横条“旧 RSP 保存前，禁止释放锁/开中断”。 | `mark_thread_running`、`switch_thread_context`、`scheduler_switch_context_and_root` |
| (d) 用户线程两根内核栈 | 分两根 32 KiB 栈：调度栈保存 user_mode_enter 最终返回位置；进入栈由 TSS.RSP0 接 syscall/IRQ。用户自身 64 KiB 栈在独立页表中。红叉表示不能用 trap frame 覆盖最终返回现场。 | `scheduler_create_user_thread`、`mark_thread_running` |
| (e) 什么时候放锁 | user 返回：完整 iret 帧 → IF=0 放锁 → iretq 开用户 IF；idle：current/RSP 稳定 → 放锁 → sti;hlt;cli → 重新拿锁。内核线程继续持锁。 | `user_mode_enter`、`idle_thread_entry` |
| (f) 不变量 | 三行：同一 TCB 不能同时跑在两核；新 current / TSS / CR3 / RSP / FX 交接必须一致；用户指令可并行，内核服务由 BKL 串行。AVX/XSAVE、线程迁移尚未实现。 | `switch_from_*`、`thread_resume_root_physical` |

底部记忆句：**新线程可见之前，旧线程必须有可恢复的完整现场。**

## 3. `images/sleep-wakeup.png`

标题：**睡眠与唤醒：等事件，别占着 CPU 转圈**。
副标题：**全局 PIT 时间 · 本核时间片 · 阻塞登记 · 跨核通知**。

| 图块 | 图面内容 | 对应函数 |
| --- | --- | --- |
| (a) 状态路线 | Running → yield → Ready；Running → sleep → Sleeping；Running → 等管道/UDP/子进程 → Blocked；事件或超时 → Ready。wake 表示可选，并非立刻执行。 | `scheduler_yield_current_thread`、`scheduler_sleep_current_thread`、`block_current_thread_internal` |
| (b) 两种时钟 | BSP PIT 单独推进全局时间；每 CPU 本地 timer 只做记账/时间片。图中四核一起跑 1 秒，全局不是 4 秒。user_ticks 归属用户线程，包含其内核服务时间。 | `scheduler_handle_timer_tick`、`scheduler_handle_local_timer_tick` |
| (c) 截止时间转换 | PIT now=100，deadline=105；scheduler total=19；remaining=5；wake_tick=24。红叉“不能拿 105 直接跟 19 比”。0/MAX=无限；过期/加法溢出拒绝。 | `relative_block_deadline`、`scheduler_block_current_thread_until` |
| (d) 原子等待 | 持 BKL → CLI → 登记等待者/再检查条件 → 标记 Blocked → 选下一线程 → 保存旧栈/切走。事件在前则不睡，事件在后则 wake，避免丢唤醒。设备自己的 wait queue 属于调用方。 | `scheduler_block_current_thread_and_enable_interrupts`、`scheduler_wait_process` |
| (e) BSP 唤醒 AP | BSP 到期扫描 → Sleeping/Blocked 改 Ready → 清 wake_tick → 入队 → 通知 assigned CPU → 该核继续执行。BSP 不替 AP 推进 AP 的时间片。 | `wake_sleeping_threads`、`wake_thread_internal`、`scheduler_wake_thread` |
| (f) 失败和粒度 | sleep(0)=yield；失败需调用方清登记；超时只是 ready 时间，不保证该刻运行；100 Hz PIT 粒度约10ms，加上排队。有限等待不要拿锁 hlt 等 BSP，否则可能死锁。 | `block_current_thread_internal`、`scheduler_sleep_current_thread` |

底部记忆句：**登记等待与切走是一件事；醒来后仍要重新检查条件。**

## 4. `images/process-reaping.png`

标题：**进程退出与回收：结束了，也不能立刻拆栈**。
副标题：**退出状态 · EOF / UDP 清理 · wait · 全 CPU current 检查**。

| 图块 | 图面内容 | 对应函数 |
| --- | --- | --- |
| (a) 创建流水线 | PCB → 私有页表 → fd/cwd → ELF → 带保护页用户栈 → TCB → ready。任何一步失败 → discard 尚未运行对象。 | `scheduler_create_user_elf_thread`、`scheduler_create_user_process` |
| (b) exit 两段路 | ring3 exit → 保存 exit_code → 恢复 user_mode_enter 的内核返回点 → 线程 Finished → 减 live；最后线程退出，进程 Exited。不要画 exit 直接 free 当前栈。 | `scheduler_exit_current_user_process`、`run_current_user_thread`、`scheduler_exit_current_thread` |
| (c) 先关资源 | 最后一条线程退出 → close fd / UDP → 管道读者收到 EOF、等待者被唤醒 → 进程保留退出码供 wait。故障退出码128+异常号。 | `scheduler_exit_current_thread`、`scheduler_handle_user_exception` |
| (d) 父进程等待 | 检查父子关系 → 子进程未退出：登记 PID、block → exit 唤醒 → 重新检查 → 读退出码；wait 本身不回收，reap 才回收。 | `scheduler_wait_process`、`scheduler_reap_process` |
| (e) 全核安全检查 | 四 CPU current 方框；CPU2 仍指向目标进程线程 → 红色禁止回收；所有 CPU 都离开后 → 删除队列引用 → free线程栈 → free页表/用户页 → 清 PCB。 | `process_is_current`、`scheduler_discard_process`、`release_thread`、`release_process` |
| (f) 孤儿与销毁 | 父退出 → 子 parent=0, auto_reap=true；已退出孤儿且全核无人使用 → 自动回收。destroy 要求全部 CPU current=null、live/sleep/blocked=0。SMP常驻 idle 不走任意运行时销毁。 | `reap_orphans`、`scheduler_destroy` |

底部记忆句：**退出先留下结果；切离当前现场，才能安全释放内存。**

## Markdown 图块映射约定

逐函数说明使用 `R(a)` 表示 ready 图(a)，`C(c)` 表示切换图(c)，`W(e)` 表示睡眠图(e)，`P(d)` 表示回收图(d)。简单名称、计数和状态格式化函数也归到相关图块，图片负责共同机制，Markdown 负责函数细节。没有函数与图块的唯一一一对应要求：多个小函数共同完成一个读者能理解的动作。

复杂度用变量 `T`=线程槽位数（32）、`P`=进程槽位数（16）、`C`=CPU数（≤4）、`Q`=本轮原始队列长度。`push_ready_thread` 自带 O(T) 槽位反查，不能把整个保序选择误画成 O(Q)：最坏为 O(Q·T)，并且首次分配还可能扫描 TCB。
