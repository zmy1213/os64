# 从“第一次真正进入用户态”到“第一版 scheduler-managed user thread”

上一轮你已经做到了：

```text
kernel_main -> user_mode_enter -> iretq -> ring 3 -> int 0x80 -> 回 kernel_main
```

这已经非常重要，
因为它证明了：

```text
ring 0 <-> ring 3 的最小切换链已经打通
```

但它还有一个明显限制：

> 那次“进入用户态”还是 `kernel_main` 亲自下场做的一次性 smoke，不是一条真正交给 scheduler 管的 user thread。

所以这一轮继续往前，目标变成：

```text
让 user code 不再只是 kernel_main 临时调一次，
而是变成：
1. 一个真正的 user process
2. 下面挂一条真正的 user thread
3. 由 scheduler 把它切上 CPU
4. 用户态 exit 后，再由 scheduler 正式回收线程
```

一句话先说结论：

```text
以前：是“内核手工做一次 ring 3 演示”
现在：已经有了“第一版由调度器真正管理的 user thread”
```

---

## 1. 为什么“已经进过一次用户态”还不够

上一轮那条路径虽然是真的，
但它更像：

```text
一次机器级演示
```

而不是：

```text
一个真正属于任务系统的执行对象
```

原因很简单。

上一轮的 `run_user_mode_smoke_test()` 里，
是 `kernel_main` 自己：

1. 构造 `UserModeLaunchContext`
2. 直接调用 `user_mode_enter()`
3. 等用户态 `exit`
4. 再继续往下跑后面的测试

也就是说，
那次用户态执行还没有真正回答下面这些更像“操作系统”的问题：

- 这段用户代码属于哪个 `process`？
- 它是不是一条真正的 `thread`？
- 它退出以后是谁回收它？
- 它和 scheduler 的关系是什么？

所以这一轮最重要的变化不是“又进了一次 ring 3”，
而是：

> 这次 ring 3 执行已经被纳入了 task/scheduler 体系。

---

## 2. 这一轮到底新增了什么

可以把这一轮理解成 4 层变化：

### 2.1 `task/` 层正式持有用户态入口现场

上一轮那份：

```text
UserModeSession
```

还写在 `kernel_main.cpp` 里，
本质上是一个“只给那次 smoke 临时用”的结构。

这一轮把它提到了：

```text
kernel/task/user_mode.hpp
```

名字也更准确地改成了：

```text
UserModeLaunchContext
```

意思是：

> 这不再只是 `kernel_main` 私有的小道具，而是 task 层正式承认的一种“用户态启动现场”。

里面保存的核心字段和上一轮一样：

- `kernel_resume_stack_pointer`
- `kernel_root_physical`
- `user_root_physical`
- `user_instruction_pointer`
- `user_stack_pointer`
- `user_rflags`
- `user_code_selector`
- `user_stack_selector`
- `return_value`

它们的布局继续被 `static_assert(offsetof(...))` 钉死，
因为汇编入口仍然按固定偏移读这些值。

---

### 2.2 `ThreadControlBlock` 现在开始区分 kernel thread 和 user thread

这一轮在 `scheduler.hpp` 里新增了：

```text
ThreadExecutionMode
```

分成两类：

- `kThreadExecutionModeKernel`
- `kThreadExecutionModeUser`

同时 `ThreadControlBlock` 里多了：

- `execution_mode`
- `user_mode`

这意味着现在一条线程不再只是：

```text
“有一个 kernel entry 函数和一根栈”
```

而是开始有两种不同启动方式：

### kernel thread

还是老路径：

```text
scheduler_switch_context -> ret -> scheduler_thread_bootstrap -> entry(context)
```

### user thread

现在变成：

```text
scheduler_switch_context -> ret -> scheduler_thread_bootstrap
-> user_mode_enter(&thread->user_mode)
-> iretq -> ring 3
```

这就是这一轮最本质的升级：

> user thread 终于也是一条真正的 scheduler 线程了。

---

### 2.3 `ProcessControlBlock` 现在可以真的是 user process

前面 PCB 已经带了 `AddressSpace`，
但还没有一条真正的 user thread 去用它。

这一轮新增了：

```text
scheduler_create_user_process(...)
```

它会：

1. 找一个空 PCB 槽位
2. 分配新的 `pid`
3. 把 `is_kernel_process = false`
4. `clone_current_address_space(...)`

所以现在这份 PCB 不只是“概念上带一份页表根”，
而是：

> 真正属于一条 user thread 的进程对象。

---

### 2.4 scheduler 现在真的会把 user thread 送进 ring 3

这一轮新增了：

```text
scheduler_create_user_thread(...)
```

和配套的 bootstrap 路径：

```text
run_current_user_thread(...)
```

调度器切到这条线程后，
不再去调用普通的 `entry(context)`，
而是：

1. 从 TCB 里拿到 `user_mode` 现场
2. 现填 `kernel_root_physical`
3. 现填 `user_root_physical`
4. 调 `user_mode_enter(...)`
5. 等它从用户态 `exit` 回来
6. 再走 `scheduler_exit_current_thread()`

这就把“用户态返回以后怎么办”这个问题，
从上一轮的：

```text
回 kernel_main
```

升级成了：

```text
回当前线程自己的 kernel 栈
-> 回 scheduler bootstrap
-> scheduler 正式把线程标 finished / 把进程标 exited
```

---

## 3. 这一轮最关键的坑：为什么一开始会 double fault

这是这一轮最值得你记住的一个坑。

第一次做 scheduler-managed user thread 时，
一启动就炸成了：

```text
double fault
```

而且 `fault_rip` 正好落在：

```text
user_mode_enter
```

里切完 `CR3` 之后，
第一次 `push SS` 的位置。

### 3.1 表面现象

流程是这样的：

1. 当前线程正在自己的 kernel thread 栈上跑
2. `user_mode_enter()` 先保存寄存器
3. 切 `CR3` 到 user process 的页表根
4. 准备往“当前这根 kernel 栈”继续压 `iretq` 帧
5. 一压就炸

### 3.2 根因

根因不是 `iretq` 本身，
而是：

```text
那根 kernel thread 栈原来是从 heap 里分的
```

而当前这个教学内核还有一个现实限制：

```text
heap 虚拟区和第一版用户区窗口还存在重叠
```

更具体地说：

1. `scheduler_create_user_process()` 先 clone 了当前页表根
2. 之后才给 user thread 分配 heap 栈
3. 所以新 clone 出来的 user root 根本没这根新栈的映射

结果就是：

```text
切到 user root 以后，
CPU 还想继续往旧 kernel stack 上 push，
但那根栈在这份新页表里根本不可见
```

于是第一条 `push` 就 page fault，
而这时如果异常路径再站不稳，
很容易继续升级成 double fault。

---

## 4. 这一轮是怎么修这个坑的

这一步没有硬去改成：

- 高半区内核
- 全局内核映射模板
- 每个地址空间自动继承完整 kernel heap 映射

因为那会把这一步一下抬得太复杂。

这一轮采取的是一个非常教学化、但完全合理的修法：

> user thread 不再用 heap 栈做“进入用户态前后的 kernel-resume 栈”，而是改用低地址 identity-mapped 栈。

也就是：

1. 从 `PageAllocator` 分 1 页物理页
2. 要求它仍落在当前 boot identity map 能直接访问的低地址范围
3. 把这张页直接当作 user thread 的 kernel-resume 栈

这样做的好处是：

- 当前 kernel root 下当然能访问
- clone 出来的 user root 里，本来就已经有同样的恒等映射
- 切 `CR3` 之后，`user_mode_enter()` 继续往这根栈上压 `iretq` 帧也不会丢映射

一句话记住这个坑：

```text
切到另一份 CR3 以后，
你脚下站着的那根 kernel 栈也必须还看得见
```

这就是为什么“用户态入口栈帧”这种事，
永远不只是 `iretq` 指令本身的问题，
而一定连着：

- 页表布局
- kernel stack 选址
- 地址空间继承策略

---

## 5. 这一轮 `exit` 为什么说“已经回到调度器了”

上一轮的 `exit` 更像：

```text
从用户态回到 kernel_main 那次测试函数
```

这一轮还是沿用了同一个：

```text
int 0x80 -> exit syscall -> user_mode_resume_kernel(...)
```

但“回去的地方”变了。

现在回去的是：

```text
这条 user thread 自己在 scheduler_thread_bootstrap 里的调用点
```

所以控制流变成：

```text
user thread 进入 ring 3
-> int 0x80 exit
-> 回到这条线程自己的 kernel bootstrap
-> scheduler_exit_current_thread()
```

这和上一轮相比，最大的区别就是：

```text
退出用户态以后，线程生命周期已经重新回到 scheduler 手里了
```

也就是：

- `thread->state = finished`
- `process->state = exited`
- `live_thread_count` 递减
- 当前线程切给下一个 runnable thread，或者切回 bootstrap 栈

这才更像真正 OS 里的线程退出路径。

---

## 6. 这一轮正常日志该怎么看

现在会多出一组新的关键标记：

- `user_thread_pid=5`
- `user_thread_tid=11`
- `user_thread_root=0x...`
- `user_thread_code_phys=0x...`
- `user_thread_stack_phys=0x...`
- `user_thread_entry=0x0000000000400000`
- `user_thread_stack_top=0x0000000000800000`
- `user_thread_program_size=...`
- `user_thread_return_cs=0x0000000000000043`
- `user_thread_return_cpl=3`
- `user_thread_process_state=exited`
- `user_thread_state=finished`
- `user thread ok`

它们分别证明了：

1. 这次不只是有用户态，而是真的新建了一个 user process / user thread
2. 这条 user thread 也有自己的用户代码页和用户栈页
3. 这条 user thread 真的进了 ring 3
4. 它退出后，线程状态被 scheduler 正式回收到 `finished`
5. 它所属的 process 也到了 `exited`

---

## 7. 这一轮还没有完成什么

虽然这一步已经比上一轮更像真正 OS，
但它还不是完整用户进程系统。

还没做的包括：

- 用户线程被 timer 抢占后再返回用户态
- 多条 user thread 共存
- 用户线程阻塞后再恢复
- ELF 用户程序加载
- 用户态页错误恢复

所以这一步更准确的名字是：

```text
第一版 scheduler-managed user thread
```

不是：

```text
完整用户进程子系统
```

---

## 8. 这一轮之后，下一步最合理做什么

最合理的下一步通常不是马上去写更多 shell 命令，
而是继续把 user thread 变得更“正式”：

1. 这一步后来已经继续推进成“每进程独立 syscall context / fd 表”，可以接着看 [KERNEL_PROCESS_SYSCALL_CONTEXT_GUIDE.md](./KERNEL_PROCESS_SYSCALL_CONTEXT_GUIDE.md)
2. 这一步后来又继续推进成“正式 `UserTrapFrame` + 每用户线程独立内核进入栈 + user yield/resume”，可以接着看 [KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md](./KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md)
3. 再往后才是真正的 ELF 用户程序加载和用户地址空间布局升级

一句话记住这一轮：

```text
上一轮证明“我能进 ring 3”
这一轮证明“我能把 ring 3 代码当成一条真正由 scheduler 管的 thread”
```
