# 从第一版 `stdin/read(0)` 到第一版 `process/thread/scheduler`

这一步开始，`os64` 不再只是“一个单内核上下文从头跑到尾”。

现在真正补的是：

> 第一版任务系统骨架：`Process + Thread + Scheduler`

但要先说清楚：

```text
这不是完整现代调度器
也不是完整抢占式内核
更不是 ring 3 用户态系统
```

这一轮的目标很明确：

```text
先让内核里第一次出现“可被调度的执行体”
```

也就是：

- 有进程对象
- 有线程对象
- 每个线程有自己的独立栈
- 调度器能在多个线程之间切换
- timer IRQ 能给当前线程记账
- 时间片用完后，系统会发出“该换人了”的请求

---

## 1. 为什么现在先做这个

你前面已经有了很多“像 OS 的基础设施”：

- 启动链
- 中断和异常
- 页分配器
- 页表
- 内核堆
- 键盘输入
- shell
- 文件系统
- fd
- syscall

但还有一个很大的空洞：

```text
系统里到底“谁在执行”
```

现在的 shell 虽然能工作，
但本质上还是：

```text
kernel_main()
  -> 初始化
  -> 直接进入 shell 死循环
```

这说明当前系统更像：

```text
一个会交互的单执行流内核
```

还不是：

```text
一个真的会管理多个执行体的操作系统
```

所以这一步的真正意义是：

> 把“内核里只有 1 条主线在跑”推进成“内核已经能管理多个线程轮流跑”。

---

## 2. 为什么不一上来就做完整抢占式调度

很多人一看到“调度器”，
会马上想到：

- timer IRQ 一到就立刻切线程
- 用户态 / 内核态栈切换
- TSS
- 阻塞队列
- 唤醒机制
- 优先级
- 多核

这些以后都要做，
但现在不能一口气全上。

原因是如果你现在直接跳到“完整抢占式 + 用户态”，
你会同时面对：

- 上下文切换汇编
- 线程栈布局
- timer IRQ 重入
- 阻塞态和 ready 队列
- 锁
- 当前进程 / 当前线程
- ring 3 / ring 0
- 用户栈
- TSS
- 用户指针检查

这会让问题缠在一起，
最后你很难知道：

```text
到底是调度器错了
还是上下文切换错了
还是用户态切换错了
```

所以这一步故意先做成：

```text
cooperative kernel thread
+ timer 驱动的 reschedule 请求
+ 安全点切换
```

换句话说：

```text
timer 先负责“提醒该换人了”
真正切栈先在安全点做
```

这比“一上来在 IRQ 里强制切栈”更容易讲清楚，也更容易调试。

---

## 3. 这一轮到底新增了哪些概念

### 3.1 `ProcessControlBlock`

第一版进程对象先只负责回答这些问题：

- 这个进程的 `pid` 是多少
- 它是不是内核进程
- 它现在大概是 `ready/running/exited`
- 它名下还有多少活线程
- 它累计消耗了多少 tick

也就是说现在的 `process` 先不是“完整资源容器”，
而是：

```text
线程的拥有者 + 一个更像真实 OS 的管理对象
```

这很重要，
因为以后你做：

- 每进程 fd 表
- 每进程 cwd
- 每进程地址空间
- 每进程权限

都需要先有这个“拥有者对象”。

### 3.2 `ThreadControlBlock`

线程才是调度器真正切换的对象。

第一版线程对象里最关键的是：

- `tid`
- `state`
- `owner`
- `entry`
- `entry_context`
- `stack_allocation`
- `saved_stack_pointer`

一句话理解：

```text
process = “谁拥有资源”
thread  = “谁真的在 CPU 上跑”
```

### 3.3 `SchedulerState`

调度器状态现在主要有：

- per-priority ready queue
- current thread
- idle thread
- time slice
- sleeping / blocked 线程计数
- total switches
- total yields
- preempt request count

现在的调度策略已经不是“所有线程排一个大队”了，
而是先做一版非常粗粒度、但已经更像真实内核的策略：

```text
priority-aware round-robin
```

也就是先按优先级选：

- `high`
- `normal`
- `background`

如果某个优先级里有多个线程，
那就在这个优先级内部做 round-robin：

> 谁先进入这个优先级的 ready queue，谁先被拿出来跑；跑完一片以后，再排到后面。

如果现在一个普通线程都没有 ready，
但系统里还有线程只是暂时 `sleeping` / `blocked`，
那 CPU 就先落到 `idle thread`。

这样做的原因很简单：

```text
先把“优先级”和“等待状态”这两个最重要的调度维度补出来，
但还不一下子跳到完整公平调度器 / 多核调度器。
```

---

## 4. 第一版上下文切换到底怎么做

这一轮新加了：

- `kernel/task/context_switch.asm`

它做的不是“保存整个 CPU 世界”，
而是先只保存：

```text
callee-saved 寄存器 + RSP
```

为什么现在这样就够？

因为当前切换点都在 C++ 函数边界附近：

- `scheduler_run_until_idle()`
- `scheduler_yield_current_thread()`
- `scheduler_exit_current_thread()`

按调用约定：

- caller-saved 寄存器本来就允许被 clobber
- 真正必须跨调用保住的是 callee-saved 寄存器

所以这一轮的上下文切换先只保存：

- `rbp`
- `rbx`
- `r12`
- `r13`
- `r14`
- `r15`
- `rsp`

这就是第一版最小可用线程切换骨架。

---

## 5. 新线程第一次为什么能“凭空跑起来”

一个已经运行过的线程，
它的 `saved_stack_pointer` 很好理解：

```text
就是上次切走时留下来的栈位置
```

但一个从来没跑过的线程，
根本没有“上次留下的栈”。

所以这一轮在创建线程时，
会手工伪造一份初始栈帧，
让调度器第一次切进去时，
最后的 `ret` 能落到：

```text
scheduler_thread_bootstrap()
```

然后 bootstrap 再去做两件事：

1. 找到当前线程对象
2. 调用这个线程真正的 `entry(context)`

如果线程函数返回了，
bootstrap 会立刻调用：

```text
scheduler_exit_current_thread()
```

所以你可以把它理解成：

> 新线程第一次不是“从函数调用进来”，而是“靠我们手工搭好的初始栈被切进去”。

---

## 6. timer 在这一轮到底起了什么作用

现在 `PIT IRQ0` 不再只会：

```text
g_timer_ticks++
```

它现在还会调用：

```text
scheduler_handle_timer_tick()
```

第一版做的事情很克制：

1. 给当前线程 `consumed_ticks++`
2. 给当前进程 `total_thread_ticks++`
3. 时间片耗尽时，把 `preempt_requested = true`

注意：

```text
这里只是“发请求”
不是“在 IRQ 里立刻切线程”
```

这点非常关键。

---

## 7. 那真正的线程切换发生在哪里

这一轮切换先放在“安全点”。

目前已经接进了 3 类地方：

1. `timer_wait_ticks()` 这种本来就 `hlt` 等下一次中断的地方
2. `console_read_line_with_history()` 这种本来就在等键盘输入的地方
3. `sys_read(fd=0)` 这种本来就在等 `stdin` 字符的地方

它们在从 `wait_for_interrupt()` 醒来以后，
都会看一眼：

```text
scheduler_yield_if_requested()
```

如果 timer 已经说“时间片到了”，
并且 ready queue 里确实还有别的线程能跑，
那就在这里切过去。

所以当前模型是：

```text
IRQ 里记账和发请求
安全点里真正切换
```

---

## 8. 这一轮的烟测到底验证了什么

这一轮在 `kernel_main` 里继续扩了 `run_scheduler_smoke_test()`。

它现在不再只测“两个线程能不能 ABAB 轮转”，
而是拆成 3 段：

### 8.1 priority 烟测

先创建 4 个线程：

- 1 个 `high`
- 2 个 `normal`
- 1 个 `background`

其中两个 `normal` 线程各跑 2 轮，
每轮都等 1 个 tick，
专门拿来放大“同优先级 round-robin”的轨迹。

所以最终日志里应该看到：

```text
sched_priority_trace=HABABC
```

这行表示：

- `H` 先跑，说明 `high` 压过 `normal/background`
- `A` / `B` 交替，说明同优先级里不是乱选，而是真的轮转
- `C` 最后才跑，说明 `background` 只会在前面优先级清空后才被调度

### 8.2 sleep + idle 烟测

再创建两个 `normal` 线程 `A` / `B`：

- 每跑一轮就先记一个字符
- 然后调用 `scheduler_sleep_current_thread(1)`

这样当两个线程都睡着时，
ready queue 会暂时清空，
CPU 必须落到 `idle thread`，
等下一个 timer tick 把睡眠线程唤醒后再继续跑。

所以最终日志里应该看到：

```text
sched_sleep_trace=ABAB
sched_idle_priority=idle
sched_idle_dispatches=2
```

这说明：

- sleep 的线程真的离开了 ready queue
- 没有普通线程可跑时，idle thread 真的顶上来了
- timer tick 到来后，sleeping 线程又真的被唤醒了

### 8.3 block + wake 烟测

最后再创建两个 `normal` 线程：

- waiter：先写 `B`，然后 `block`
- waker：先等 1 个 tick，再写 `W`，再去唤醒 waiter

所以最终日志里应该看到：

```text
sched_block_trace=BWb
```

这说明：

- waiter 不是直接跑到底，而是真的在中途挂起了
- 另一个线程可以通过 `scheduler_wake_thread()` 把它放回 ready queue
- waiter 被唤醒后，会从 block 之后那一行继续跑，而不是重新从头开始

最后你还会看到：

- `sched_priority_pid=1`
- `sched_sleep_pid=2`
- `sched_block_pid=3`
- `sched_sleeping_after=0`
- `sched_blocked_after=0`
- `sched_ready_after=0`
- `sched_live_after=0`

这说明当前这版已经不仅是“能切换线程”，
而是已经能把线程放进不同状态，再正确收回来。

---

## 9. 这一轮改了哪些文件

核心新增文件：

- `kernel/task/scheduler.hpp`
- `kernel/task/scheduler.cpp`
- `kernel/task/context_switch.asm`

接入点改动：

- `kernel/interrupts/pit.cpp`
- `kernel/console/console.cpp`
- `kernel/syscall/syscall.cpp`
- `kernel/core/kernel_main.cpp`

构建和测试：

- `scripts/build-stage1-image.sh`
- `scripts/test-stage1.sh`
- `scripts/test-invalid-opcode.sh`
- `scripts/test-page-fault.sh`

文档：

- `docs/KERNEL_TASKING_GUIDE.md`
- `docs/README.md`
- `README.md`
- `kernel/README.md`

---

## 10. 这一轮还没实现什么

一定要把边界看清楚。

现在还没有：

1. ring 3 用户线程
2. TSS
3. 用户栈 / 内核栈切换
4. 每进程地址空间
5. 让 `stdin/read(0)` / 磁盘 I/O / IPC 真正接进 blocked queue
6. 抢占式“IRQ 里直接切栈”
7. 锁 / 自旋锁
8. 公平性策略、动态优先级、饥饿避免
9. 多核调度
10. 已退出线程栈的正式回收

所以当前更准确的说法是：

```text
第一版带优先级 / sleep / wake 的 kernel tasking skeleton
```

而不是：

```text
完整现代调度器
```

---

## 11. 为什么这一步仍然很重要

因为从这里开始，
你的内核里第一次真正出现了：

```text
“执行体”
```

以后很多东西都会建立在这个层上：

- shell 不再直接挂在 `kernel_main`
- 文件描述符表可以从“全局一张”变成“每进程一张”
- cwd 可以从“全局一个”变成“每进程一个”
- `sleep` 现在已经可以真的把线程挂起
- `read(0)` 现在也已经可以真的阻塞当前线程，再由键盘 IRQ 唤醒
- 最后才是用户态程序

一句话说：

> 这一步不是把调度器做完，而是把“以后谁来被调度”这件事第一次做成真对象。

---

## 12. 现在怎么测试

正常启动链：

```bash
make test-stage1
```

非法指令异常：

```bash
make test-invalid-opcode
```

页错误异常：

```bash
make test-page-fault
```

这一轮新的关键日志包括：

```text
sched_priority_pid=1
sched_priority_trace=HABABC
sched_sleep_trace=ABAB
sched_block_trace=BWb
sched_idle_priority=idle
sched_sleeping_after=0
sched_blocked_after=0
scheduler ok
```

---

## 13. 这一步做完以后，下一步最合理是什么

上面当时建议的两件事：

- 把 shell 接进 scheduler
- 把 console/shell 的输入等待改成真正 block

现在已经在下一篇里真正实现了：

- [从第一版 `process/thread/scheduler` 到“shell 真正跑进调度器”](./KERNEL_SCHEDULER_SHELL_GUIDE.md)

所以现在再往前，最合理的重点就从“先把交互路径挂进线程模型”变成：

### 路线 A：开始补用户态切换前置条件

比如：

- TSS
- ring 3 / ring 0 栈切换
- 用户态返回路径

### 路线 B：把地址空间真正做成“每进程一份”

比如：

- `ProcessControlBlock` 里挂独立页表根
- 内核高地址共享、用户低地址独立
- `fork/exec` 之前先把“创建空用户地址空间”走通

### 路线 C：把抢占和同步补完整

比如：

- 真正从 timer IRQ 触发内核抢占
- 自旋锁
- 关中断临界区
- 线程等待队列抽象

一句话说：

> 现在 shell 和 console 已经真正站到 scheduler 这条线上了，下一步就该开始把“只有内核线程的教学内核”继续推进成“能切用户态、能管每进程地址空间”的系统。
