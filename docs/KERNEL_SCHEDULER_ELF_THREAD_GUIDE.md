# 从“第一版 scheduler-managed user thread”到“scheduler 正式接管 ELF 用户线程”

先说这一步到底做了什么：

> 上一轮我们已经有两条线了：
> 1. scheduler 已经会跑一条真正的 user thread
> 2. 内核已经会把 `/docs/hello.elf` 当成 ELF64 文件解析并执行
>
> 这一轮继续往前，把这两条线真正接起来：
> scheduler 不再只会跑“内嵌在内核里的测试用户程序”，而是已经能创建 user process、装入 `/docs/hello.elf`、再把 ELF 的 entry 当成一条正式 user thread 跑完退出。

如果你是小白，可以先把它理解成一句话：

> 以前 ELF 只是 `kernel_main` 手工点火跑一次；现在它第一次真的进入“进程/线程/调度器”这套正式路径里。

---

## 1. 为什么这一步重要

前一轮的 `ELF loader` 已经证明了一件大事：

```text
文件系统里的 ELF 文件
-> 读入 staging page
-> 校验 ELF header / program header
-> 把 PT_LOAD 映射进用户地址空间
-> 从 entry 进入 ring 3
```

但是那一轮还有一个明显局限：

> 它还是 `kernel_main` 手工做的一次性 smoke。

也就是说，内核那时只是证明：

- “我会读 ELF”
- “我会进 ring 3”

但它还没有证明：

- “调度器也会跑 ELF 用户线程”
- “ELF 程序已经真正进入 process/thread 这一层”
- “ELF 线程退出后，调度器也知道怎么把它回收到 `finished`”

现代内核里，程序格式解析和线程调度不是两件孤立的事。

它们最后必须连成一条路径：

```text
创建进程
-> 建地址空间
-> 装程序
-> 建用户栈
-> 建线程
-> 交给 scheduler 运行
-> 退出后回到调度器
```

这一步做的就是这件事。

---

## 2. 这一轮新增了什么

这一轮核心上新增了两样东西：

1. `kernel/task/scheduler.cpp/.hpp` 里新增了正式 helper：

```text
scheduler_create_user_elf_thread(...)
```

2. `kernel/core/kernel_main.cpp` 里新增了一条独立 smoke：

```text
run_scheduler_elf_thread_smoke_test(...)
```

这两样东西合起来的意思是：

> 现在“从 ELF 文件到 user thread”的这段桥，不再散落在 `kernel_main` 里临时手工拼起来，而是开始有了第一版正式封装。

---

## 3. 为什么要新增 helper，而不是继续在 `kernel_main` 里手写

如果继续全写在 `kernel_main`，
每次都得手工重复这些步骤：

1. 创建 user process
2. 初始化这份进程自己的 `fd` / `cwd` / `syscall context`
3. 把 ELF 装进这份进程的地址空间
4. 给它补一页用户栈
5. 再创建 user thread

这会有两个问题。

### 第一，路径太散

你会看不出“正式启动一个 ELF 用户线程”到底有哪些固定步骤。

### 第二，后面不好扩

以后你还会继续做：

- 多段 `PT_LOAD`
- 更正式的 `exec`
- 命令行参数 / 用户栈布局
- `fork/exec/wait`

如果现在不先把入口整理出来，
后面每次都得继续复制粘贴。

所以这一轮把桥先收成一个最小正式接口，是合理的。

---

## 4. `scheduler_create_user_elf_thread()` 这一步到底帮你做了什么

你可以把这个 helper 理解成：

> “第一版从 ELF 文件创建用户进程 + 用户线程”的启动器。

它现在按顺序做了 5 件事。

### 第一步：先创建 user process

先调用：

```text
scheduler_create_user_process(...)
```

原因是：

> ELF 不是装进“某条线程”里，而是先装进“某个进程的地址空间”里。

线程只是“从这份地址空间里的某个 entry 开始执行”。

所以顺序必须先有 process，再有 thread。

### 第二步：初始化这份进程自己的 syscall 视图

接着调用：

```text
scheduler_initialize_process_syscall_view(...)
```

它会把这些东西挂进新进程：

- `FileDescriptorTable`
- `SyscallContext`
- 默认 `cwd`
- `stdout/stderr` 的写出口

为什么要先做这个？

因为这份 ELF 程序一进用户态就会发 `write` syscall。

如果进程还没有自己的 syscall 视图，
它虽然“跑起来了”，
但系统调用落地时就没有正确的上下文可用。

### 第三步：把 ELF 段装进这份进程地址空间

然后 helper 会调用：

```text
load_elf_user_program(...)
```

这一步会：

- 打开 `/docs/hello.elf`
- 读入 staging page
- 校验 ELF header
- 找到唯一的 `PT_LOAD`
- 把它映射到用户虚拟地址 `0x400000`
- 记录真正入口地址 `0x400080`

也就是说：

> 进程地址空间里现在第一次真正出现了一段“来自 ELF 文件”的用户代码。

### 第四步：再补 1 页用户栈

ELF loader 当前只负责“程序段”。

用户线程真正跑起来之前，还必须自己有栈。

所以 helper 还会：

1. 分 1 张物理页
2. 清零
3. 映射到用户栈页
4. 把用户初始 `RSP` 设在页顶

当前这一步继续保持最简单的教学布局：

```text
1 条 ELF 用户线程
= 1 份用户程序段
= 1 页初始用户栈
```

### 第五步：最后再创建 user thread

前面 4 步都准备好以后，
才调用：

```text
scheduler_create_user_thread(...)
```

这里传进去的关键参数是：

- `user_instruction_pointer = ELF entry`
- `user_stack_pointer = 0x800000`
- `user_rflags = 当前先保持 IF 关闭`

这一步的本质是：

> 线程对象本身不再关心“程序是裸二进制还是 ELF”，它只接收“已经准备好的入口 RIP + 用户栈 + 用户页表根”。

这就是比较合理的分层。

---

## 5. 为什么这一步要单独做一条 `scheduler_elf` smoke，而不是直接替换原来的 user thread smoke

因为原来的 `user thread` smoke 目标更复杂。

它要验证的是：

- `yield`
- `timer preempt`
- `stdin block/wake`
- helper kernel thread
- `TSS.rsp0`
- 用户 trap frame

而这一轮要验证的是另一件事：

> “ELF 文件能不能正式接进 scheduler/process/thread 路径”

如果直接把旧 smoke 整个替换掉，
你会把两个问题搅在一起：

1. 是 ELF 加载坏了？
2. 还是 `yield/preempt/stdin` 那条复杂链坏了？

所以现在最合理的做法是：

- 保留原来的复杂 `user thread` smoke
- 另外新增一条更小、更干净的 `scheduler_elf` smoke

这样一旦失败，你很容易知道是哪条链断了。

---

## 6. 这条新 smoke 实际验证了什么

启动时现在会新增这些关键日志：

```text
scheduler_elf_pid=1
scheduler_elf_tid=1
scheduler_elf_path=/docs/hello.elf
scheduler_elf_inode=7
scheduler_elf_entry=0x0000000000400080
scheduler_elf_segment_count=1
scheduler_elf_page_count=1
user_elf_program=hello from elf
scheduler_elf_return_flags=0x0000000000000040
scheduler_elf_process_state=exited
scheduler_elf_thread_state=finished
scheduler_elf_thread ok
```

这些日志分别在证明：

1. 真的创建出了新的 user process / user thread
2. 读到的真的就是 `/docs/hello.elf`
3. ELF entry 真的是从 header 里解析出来的 `0x400080`
4. scheduler 真的把这条线程切进了 ring 3
5. 用户程序真的执行了
6. `exit` 的返回值真的回到了调度器
7. 最后 process/thread 状态真的都变成了退出完成态

---

## 7. 为什么 `scheduler_elf` 这一步先不打开 IF

这一轮的 `hello.elf` 程序只做两件事：

1. `write`
2. `exit`

它不需要：

- timer 抢占
- keyboard IRQ 唤醒
- `read(0)` 阻塞

所以这里先故意保持：

```text
IF = 0
```

原因不是“做不到”，而是：

> 这条 smoke 的重点是“ELF 能否正式接进 scheduler”，不是“它会不会被中断打断”。

把变量压到最少，最容易看清主线。

---

## 8. 这一轮完成后，系统里现在其实已经有了两条用户程序启动路线

### 路线 A：教学型直接 smoke

```text
kernel_main
-> 手工创建地址空间
-> 手工加载 ELF
-> 手工 user_mode_enter
```

这条路径适合讲：

- ELF header
- PT_LOAD
- entry point
- ring 3 第一次进入

### 路线 B：更正式的 scheduler 路线

```text
create user process
-> init process syscall view
-> load ELF into process address space
-> map user stack
-> create user thread
-> scheduler_run_until_idle
```

这条路径适合讲：

- 进程和线程分工
- 地址空间属于谁
- 为什么 thread 只是执行入口，不拥有程序镜像
- 为什么 scheduler 最后才是真正运行用户程序的人

---

## 9. 这一步之后，下一步最合理做什么

现在你已经完成的是：

> “单段、小 ELF、单线程、最小退出链”的正式接入。

后面最合理的继续方向有两个。

### 方向 1：把 ELF loader 做得更正式

例如：

- 支持多个 `PT_LOAD`
- 正式处理 `filesz < memsz` 的 BSS 语义
- 更清楚地区分只读段、可写段、可执行段

### 方向 2：把进程启动路径做得更正式

例如：

- 从 shell 里按路径启动用户程序
- 做第一版 `exec`
- 做参数栈布局
- 做 `wait` / 退出码

如果按教学顺序来说，
我建议先走方向 1。

因为：

> 你现在已经有“scheduler 跑 ELF”的外壳了，下一步更值得补的是“ELF 本身还不够完整”。

---

## 10. 一句话总结这一步

一句话记住：

> 上一轮是“内核会解析 ELF”，这一轮是“调度器也会正式运行 ELF”。

再说得更完整一点：

> `os64` 现在不只会把 ELF 文件读进来，也已经能把它放进某个 user process 的地址空间，再作为一条 scheduler-managed user thread 真的跑完退出。
