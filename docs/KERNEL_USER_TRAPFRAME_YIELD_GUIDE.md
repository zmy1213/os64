# 从“每进程 syscall context / fd 视图”到“正式 UserTrapFrame + 每用户线程内核进入栈 + user yield/resume”

这一轮解决的不是“再多一个 syscall”这么简单的问题，
而是把 user thread 从：

```text
能进 ring 3
```

推进到：

```text
能在 syscall 里主动让出 CPU
-> 先切去别的线程
-> 再切回来
-> 继续回到 ring 3 往下跑
```

如果你是小白，
可以先把这一轮理解成一句话：

> 我们让 user thread 第一次真的在“进内核 -> 切线程 -> 回用户态”这条完整链路上活了下来。

---

## 1. 上一轮已经做到哪里了

上一轮结束时，
系统已经有这些能力：

- user process 有自己的 `cwd`
- user process 有自己的 fd 表
- ring 3 代码打 `int 0x80` 时，会走“当前线程所属进程”的 syscall 上下文
- scheduler 已经能真正管理一条 user thread

也就是说，
我们已经不只是：

```text
能进一次 ring 3
```

而是已经能做到：

```text
这条 ring 3 代码属于一个真正的 user process
```

但是还差一件很关键的事情：

```text
如果 user thread 进了内核以后，中途被切走，
还能不能再切回来，并继续回到 ring 3 往下执行？
```

这正是这一轮要解决的问题。

---

## 2. 这一轮真正遇到的内核问题是什么

表面上看，
我们只是想加一个：

```text
sys_yield()
```

让用户态代码能主动说：

```text
“我现在先让别人跑一下。” 
```

但真正做起来以后，
立刻会撞上一个底层问题：

### 问题 1：用户态进内核的机器现场要保存在哪里

当 ring 3 执行：

```asm
int 0x80
```

CPU 会自动把一组“以后怎么回去”的现场压到内核栈上，
比如：

- 用户态 RIP
- 用户态 CS
- 用户态 RFLAGS
- 用户态 RSP
- 用户态 SS

再加上汇编 stub 手工保存的通用寄存器，
这就组成了一份真正的 trap/syscall frame。

如果这份现场没地方放，
或者放乱了，
后面就没法安全回到用户态。

### 问题 2：不能把所有事情都压在同一根内核栈上

这一步是这一轮最重要的坑。

一开始很容易直觉上写成这样：

```text
user thread 只有一根内核栈
```

然后既拿它做：

1. scheduler/bootstrap 的线程栈
2. `user_mode_enter()` 将来最终要退回的那根“恢复栈”
3. 每次 ring 3 进 ring 0 时 `TSS.rsp0` 指向的入口栈

这看起来省事，
但实际上是错的。

原因是：

- `user_mode_enter()` 会先把“以后最终怎么回到线程入口”的返回现场放到这根栈上
- 后面每次用户态 `int 0x80`，CPU 又会从 `TSS.rsp0` 开始往下压一整份新的 trap frame

如果这两件事共用同一页栈，
新的 syscall/trap frame 就会把最早那份“最终返回现场”踩坏。

最后的后果就是：

```text
用户线程前面的 syscall 都可能看起来正常，
但等真正 exit 时，内核已经找不到原来该退回的位置了
```

这一轮调试时，
这个问题就真发生过。

---

## 3. 所以这一轮为什么必须拆成“两根内核栈”

现在 user thread 明确拆成了两根 low identity-mapped 内核栈：

### 第一根：scheduler/bootstrap 栈

这根栈负责：

- `scheduler_switch_context()` 第一次把线程切进去
- `scheduler_thread_bootstrap()` 在内核里开始执行
- `run_current_user_thread()`
- `user_mode_enter()` 保存“最终要回到哪里”的恢复现场
- 最后 `exit` 时 `user_mode_resume_kernel()` 退回这里

你可以把它理解成：

```text
“这条线程自己的主内核栈”
```

### 第二根：user kernel entry stack

这根栈专门给：

- `TSS.rsp0`
- ring 3 -> ring 0 的 `int 0x80`
- 以后来自用户态时的中断 / 异常入口

你可以把它理解成：

```text
“用户态每次进内核时，临时落脚的专用入口栈”
```

这样分开以后：

- syscall/trap frame 压在“入口栈”上
- `user_mode_enter()` 最终恢复现场留在“主内核栈”上

两者互不覆盖。

这就是这一轮最核心的结构升级。

---

## 4. 这一轮具体新增了什么

### 4.1 正式的 `UserTrapFrame`

以前系统里虽然已经能从 ring 3 打回内核，
但“机器现场”更多还是隐含在汇编压栈顺序里。

现在把它正式写成了一个结构体：

```text
UserTrapFrame
```

里面明确保存：

- `r15..rax`
- `vector`
- `error_code`
- `rip`
- `cs`
- `rflags`
- `rsp`
- `ss`

这样做的意义是：

1. 把“汇编里的隐含现场”变成 C++ 里可观察的数据结构
2. 后面做更正式的用户态恢复时，有明确对象可依赖
3. 串口日志和 smoke test 终于能直接验证“最近一次用户态进内核”到底保存了什么

### 4.2 `sys_yield`

新增了一条 syscall 编号：

```text
kSyscallNumberYield = 11
```

它现在的目标很单纯：

```text
让当前 user thread 主动让出 CPU
```

内核接到它以后，
会：

1. 给当前线程记一次 `user_yield_count`
2. 调 `scheduler_yield_current_thread()`
3. 把当前线程放回 ready queue
4. 先切去别的线程

### 4.3 每次用户态 syscall 都会捕获 trap frame

`kernel_handle_syscall()` 现在在真正分发 syscall 之前，
会先判断：

```text
这次是不是从 ring 3 进来的？
```

如果是，
就把这次现场抄进：

```text
current_thread->user_trap_frame
```

所以现在 `ThreadControlBlock` 已经开始真的记住：

```text
“我最近一次从用户态进内核时，CPU 当时是什么状态”
```

### 4.4 `TSS.rsp0` 现在会跟着当前 user thread 切换

以前 `TSS.rsp0` 更像一份全局默认值。

现在变成：

- 当前跑的是 user thread：
  `TSS.rsp0 = thread->user_kernel_entry_stack_top`
- 当前跑的是普通 kernel thread：
  回退到默认内核 `RSP0`

这意味着：

```text
不同 user thread 已经开始真的拥有自己的“用户态进内核入口栈”
```

这就是 long mode 下以后做更正式用户态的重要基础。

---

## 5. 这一轮完整执行路径到底是什么

如果把这一轮缩成一条故事线，
大概是这样：

### 第 1 步：scheduler 先把 user thread 切上 CPU

调度器选中 `user-main`，
在它自己的 scheduler/bootstrap 栈上进入：

```text
scheduler_thread_bootstrap()
-> run_current_user_thread()
-> user_mode_enter()
```

### 第 2 步：`user_mode_enter()` 真正进 ring 3

它会：

1. 先把“最终要怎么退回内核”保存到主内核栈
2. 切到 user process 的 `CR3`
3. 伪造 `iretq` 需要的 ring 3 返回帧
4. 执行 `iretq`

然后 CPU 就真正落到用户代码页里执行。

### 第 3 步：用户程序先做普通 smoke

user program 会先验证：

- `write(1, ...)`
- `getcwd`
- 用相对路径读 `readme.txt`

所以串口里会先看到：

- `user_mode_message=hello from ring3 via int80`
- `user_mode_cwd=/`
- `user_mode_readme_prefix=os64fs readme`

### 第 4 步：用户程序进入 `yield`

然后它打印：

```text
user_mode_yield_before=1
```

接着执行：

```asm
mov eax, SYSCALL_YIELD_NUMBER
int 0x80
```

这一次 `int 0x80` 进内核时，
CPU 会先切到：

```text
thread->user_kernel_entry_stack_top
```

也就是 user thread 的专用内核进入栈。

### 第 5 步：内核捕获 trap frame，然后切去 helper thread

`syscall_interrupt_stub` 会先把寄存器压栈，
`kernel_handle_syscall()` 会把这份现场抄进：

```text
thread->user_trap_frame
```

然后 `sys_yield` 调：

```text
scheduler_yield_current_thread()
```

这时当前 user thread 并不会被结束，
而是：

- 保留住自己“暂停在 syscall 里面”的内核入口栈
- 自己先回到 ready queue
- 切给 `user-yield-helper` 线程

### 第 6 步：helper thread 先跑一次

helper thread 是这一轮故意加的“旁证线程”。

它的作用不是做业务，
而是强迫系统证明：

```text
yield 以后真的切走了，不是只在原地假装返回
```

所以日志里会看到：

- `user_thread_helper_tid=12`
- `user_thread_helper_runs=1`

### 第 7 步：再切回暂停在 syscall 里的 user thread

helper thread 也会再让出 CPU，
调度器把之前暂停的 user thread 切回来。

关键点是：

这次切回来的不是“重新从头跑用户程序”，
而是：

```text
继续回到那次还没结束的 syscall 内核路径里
```

于是 `kernel_handle_syscall()` 正常返回，
汇编 stub 恢复寄存器，
最后 `iretq` 回到 ring 3。

### 第 8 步：用户程序验证“我真的活着回来了”

回到 ring 3 以后，
用户程序会检查：

- `rax == 0`
- `r15` / `r14` 这些关键寄存器值没坏

如果都对，
再打印：

```text
user_mode_yield_after=1
```

并把一个新的结果位塞进最终返回值。

所以：

```text
user_thread_return_flags=0x0000000000000007
```

里的 `0x4`，
就是这一步新增的：

```text
USER_MODE_RESULT_YIELD_RESUME_OK
```

### 第 9 步：最后 `exit`，正式退回 scheduler

用户程序最后再打一趟 `exit` syscall。

这一次 `kernel_handle_user_mode_exit()` 会调用：

```text
user_mode_resume_kernel(...)
```

把：

- `CR3` 切回内核页表
- 栈切回最早 `user_mode_enter()` 保存的主内核栈
- 控制流退回 `run_current_user_thread()`

然后线程正式走：

```text
scheduler_exit_current_thread()
```

这时它才真正结束。

---

## 6. 这一轮串口里要重点看哪些标志

现在这组标志最重要：

- `user_mode_yield_before=1`
- `user_mode_yield_after=1`
- `user_thread_helper_tid=12`
- `user_thread_helper_runs=1`
- `user_thread_yield_count=1`
- `user_thread_return_flags=0x0000000000000007`
- `user_thread_kernel_entry_stack_top=0x...`
- `user_thread_tss_rsp0=0x...`
- `user_thread_trap_cs=0x0000000000000043`
- `user_thread_trap_ss=0x000000000000003B`
- `user_thread_trap_rip=0x000000000040...`
- `user_thread_trap_rsp=0x0000000000800000`

它们分别说明：

1. user thread 在 `yield` 之前和之后都真的跑到了用户态
2. 中间确实切去过另一条 helper thread
3. `yield` 不是假动作，调度器真的参与了
4. `TSS.rsp0` 已经不再是单一全局入口，而是跟当前 user thread 绑定
5. trap frame 已经被正式保存下来，可供后面继续扩展

---

## 7. 这一轮改了哪些文件，为什么改

这一步关键改动主要在这些文件：

- `kernel/task/user_mode.hpp`
  增加 `UserTrapFrame`，把用户态进内核的机器现场正式数据化。
- `kernel/task/scheduler.hpp`
  给 `ThreadControlBlock` 增加 trap frame、yield 计数和 user 专用内核进入栈字段。
- `kernel/task/scheduler.cpp`
  真正把 user thread 拆成“两根内核栈”，并在调度时切换 `TSS.rsp0`。
- `kernel/syscall/syscall.hpp` / `kernel/syscall/syscall.cpp`
  增加 `sys_yield` 编号，并在用户态 syscall 入口捕获 trap frame。
- `kernel/interrupts/interrupts.hpp` / `kernel/interrupts/interrupts.cpp`
  让内核能读出/更新当前 `TSS.rsp0`。
- `kernel/task/context_switch.asm`
  增加自包含的 `yield` 用户程序，用来验证“切走再回来”。
- `kernel/core/kernel_main.cpp`
  增加新的 smoke test、helper thread、日志和断言。
- `scripts/test-stage1.sh`
- `scripts/test-page-fault.sh`
- `scripts/test-invalid-opcode.sh`
  把测试同步到新的真实行为，比如 `return_flags=0x7`、helper thread、yield 标志和新的 shell TID。

---

## 8. 这一步为什么还不是“完整用户态恢复方案”

虽然这一步已经很关键，
但它还不是最终形态。

现在的恢复路径仍然是：

```text
先把 user thread 暂停在 syscall 里面
-> 切去别的线程
-> 再切回来继续把这次 syscall 跑完
-> 再 iretq 回 ring 3
```

也就是说，
它还不是：

```text
拿着保存好的 UserTrapFrame，
在任意时刻直接重建现场并回到用户态
```

后者会更接近将来真正的：

- 抢占后返回用户态
- 信号/异常返回
- 更正式的用户线程阻塞/唤醒

但现在这一步已经足够重要，
因为它先证明了：

```text
user thread 可以暂停在内核里，再回来，最后还能继续回到 ring 3
```

---

## 9. 这一轮之后，下一步最合理做什么

这一步后来已经继续推进成“第一版 user timer preemption”，
可以接着看 [KERNEL_USER_TIMER_PREEMPT_GUIDE.md](./KERNEL_USER_TIMER_PREEMPT_GUIDE.md)。

如果只站在这一轮当时的视角，
最合理的下一步通常还是继续把“恢复用户态”做得更正式：

1. 让保存下来的 `UserTrapFrame` 不只是可观察，而是能真正参与恢复路径
2. 让 timer 抢占也能安全切走 user thread，再回到 ring 3
3. 做更像样的用户程序加载器，而不是只靠内嵌汇编 smoke program
4. 再往后才是 `fork/exec/wait` 这种更完整的用户进程模型

一句话记住这一轮：

```text
上一轮证明“user thread 能进 ring 3”
这一轮证明“user thread 能在 syscall 里切走，再恢复回 ring 3 继续执行”
```
