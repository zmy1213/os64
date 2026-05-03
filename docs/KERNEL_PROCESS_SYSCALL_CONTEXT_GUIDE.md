# 从“第一版 scheduler-managed user thread”到“每进程 syscall context / fd 视图”

上一轮你已经做到了：

```text
scheduler
-> 选中一条 user thread
-> 切进 ring 3
-> 用户态 exit
-> 回到 scheduler
```

这已经很重要，
因为它说明：

```text
user thread 不再只是 kernel_main 的一次性演示，
而是已经被 task/scheduler 体系正式接住了
```

但它还差最后一口很关键的“像进程”的气：

> 用户线程虽然已经属于某个 `process`，可它做 syscall 时，仍然还在借用全局 `g_syscall_context` 和全局 `g_fd_table`。

这意味着之前那版 user process 还没有真正回答这个问题：

```text
“这个进程自己的 cwd、fd 表、stdout/stderr 出口，到底放在哪里？”
```

所以这一轮继续往前，
目标不是马上做 `fork/exec`，
而是先把：

```text
每进程自己的 syscall 视图
```

这件事立住。

一句话先说结论：

```text
以前：user thread 已经能跑，但 syscall 还在借全局上下文
现在：PCB 已经正式带上自己的 FileDescriptorTable + SyscallContext
```

---

## 1. 为什么上一轮还不够

上一轮里，
你已经有了：

- `ProcessControlBlock`
- `ThreadControlBlock`
- `AddressSpace`
- `user thread`
- `int 0x80`

看起来像一个“进程”了，
但还缺一个非常现实的问题：

### 进程到底怎么“看文件系统”

比如两个进程：

- A 当前目录在 `/`
- B 当前目录在 `/docs`

如果它们都执行：

```text
open("readme.txt")
```

真实 OS 里，
这两个调用不应该一定指向同一个地方。

它们应该先看：

- 各自的 `cwd`
- 各自的 fd 表
- 各自的 stdout/stderr 绑定

而不是先看某个全局单例变量。

所以从“能跑 user thread”走向“更像真正进程”，
最先该补的不是更多命令，
而是：

> 把 syscall 视图从“全局内核状态”推进成“每进程状态”。

---

## 2. 这一轮到底改了什么

这一轮可以拆成 4 个关键变化。

### 2.1 PCB 现在真的带自己的 fd 表和 syscall 上下文

现在 `ProcessControlBlock` 里不再只有：

- `pid`
- `state`
- `address_space`

还新增了：

- `FileDescriptorTable file_descriptors`
- `SyscallContext syscall_context`

这件事非常重要，
因为它把“进程拥有的资源”又往前推进了一步。

以前 PCB 更像：

```text
“这是谁的页表根”
```

现在 PCB 开始变成：

```text
“这是谁的页表根 + 这是谁的 cwd/fd/syscall 视图”
```

也就是说，
`process` 终于不只是“地址空间拥有者”，
还开始成为：

```text
文件访问视图的拥有者
```

---

### 2.2 新增了一个“给进程装 syscall 视图”的初始化入口

这一步没有把 VFS 强行绑死进 `scheduler_create_*_process()` 里，
而是额外补了：

```text
scheduler_initialize_process_syscall_view(...)
```

它做的事很直接：

1. 先初始化这条进程自己的 `FileDescriptorTable`
2. 再让 `SyscallContext` 指向这张 fd 表
3. 再按需要给它装 `stdout/stderr` 写回调

为什么要拆成单独一步，
而不是在“创建进程”时一次做完？

因为“调度器创建 PCB”关注的是：

- PID
- 生命周期
- 地址空间

而“这个进程用哪个 VFS、写到哪里去”其实是更高一层的策略。

这样拆开以后，
代码层次更清楚：

- `scheduler_create_*_process()` 负责“把进程对象生出来”
- `scheduler_initialize_process_syscall_view()` 负责“给它装一套文件/系统调用视图”

---

### 2.3 `int 0x80` 分发现在优先看“当前线程属于哪个进程”

这是这一轮最核心的行为变化。

以前 `kernel_handle_syscall()` 最终分发时，
主要依赖：

```text
g_active_syscall_context
```

这意味着：

> 谁最后安装了那个全局上下文，谁就“赢了”。

这对 boot 早期 smoke 当然够用，
但对真正的 user thread 来说不对。

因为真实情况下，
CPU 正在执行哪条线程，
就应该先看：

```text
当前线程 -> owner process -> syscall_context
```

所以现在分发逻辑变成了：

1. 如果当前确实跑在线程上下文里
2. 而且这条线程属于某个进程
3. 这个进程自己的 `syscall_context` 已经准备好

那么：

```text
优先用 owner->syscall_context
```

只有在下面这种“还没有线程上下文”的早期阶段，
才退回到原来的全局默认上下文：

- boot 早期 smoke
- `kernel_main` 直接做的一些系统调用形状测试

这就是这一轮真正从“教学单例”迈向“进程视图”的地方。

---

### 2.4 用户态 smoke program 不再只是打印一句话

以前 ring 3 smoke program 的任务很简单：

1. 打一条 `write`
2. 再 `exit`

它能证明：

```text
我确实进过 ring 3
```

但它证明不了：

```text
我在 ring 3 里拿到的是不是“我自己的进程上下文”
```

所以这一轮把用户态程序升级成了真正会做两件事：

### 第一步：`getcwd`

用户态先调用：

```text
getcwd(buffer, 64)
```

并把结果打印成：

```text
user_mode_cwd=/
```

然后它还把这个结果做成“自检位”带回内核：

- 如果它看到的真是 `/`
- 而不是别的共享 cwd

就置上：

```text
kUserModeResultRootCwdOk
```

### 第二步：相对路径 `open("readme.txt")`

它接着再调用：

```text
open("readme.txt")
read(...)
close(...)
```

然后把读到的前缀打印成：

```text
user_mode_readme_prefix=os64fs readme
```

这个动作的意义非常强：

如果这条 user thread 其实还在误用全局 cwd，
那它的相对路径解析就可能跑偏。

所以这一轮不只是“会 syscall”，
而是：

> 用户态真的开始依赖“自己的 cwd + 自己的 fd 视图”做正确行为。

---

## 3. 这一轮怎么证明“不是借错了全局上下文”

这是这一步最漂亮的测试设计。

在 `scheduler-managed user thread` smoke 里，
内核故意先做一件事：

```text
把默认内核 syscall context 的 cwd 改成 /docs
```

也就是说，
如果分发逻辑还是错的，
user thread 很容易就会看到：

```text
/docs
```

或者它的相对路径 `readme.txt` 会解析错。

但现在真正的结果是：

- `user_thread_kernel_cwd_before=/docs`
- `user_thread_process_cwd_before=/`
- 用户态里仍然打印 `user_mode_cwd=/`
- 用户态里仍然能成功读到 `user_mode_readme_prefix=os64fs readme`
- 退出后：
  - `user_thread_kernel_cwd_after=/docs`
  - `user_thread_process_cwd_after=/`

这正好证明了：

```text
默认内核上下文是 /docs
不等于
当前 user process 自己的上下文
```

也就是说，
现在 `int 0x80` 真正开始按：

```text
当前线程 -> 当前进程
```

来找 syscall 视图了。

---

## 4. 为什么用户态 `exit` 返回值里要塞“状态位”

这一轮里，
用户态程序最后不再只把 `CS` 带回来，
而是带回：

```text
低 16 位  = 用户态看到的 CS
高位标志 = 用户态自检结果
```

所以内核现在能同时检查两件事：

1. `CS == 0x43`
   说明它真的跑在 ring 3
2. `flags == 0x3`
   说明它真的读到了正确 cwd，并成功用相对路径读到了 `readme.txt`

这样一来，
测试不再只是：

```text
“CPU 权限级切换成功”
```

而是进一步变成：

```text
“CPU 权限级切换成功 + 进程级 syscall 视图也真的工作”
```

---

## 5. 这一轮到底实现了什么

如果把这一步翻译成“小白能听懂的话”，
你可以这样理解：

### 以前

内核已经能把一条用户线程送进用户态，
但它看文件系统时，
还像是在借别人的工具箱。

### 现在

每个进程开始有了自己的小工具箱：

- 自己的 `cwd`
- 自己的 fd 表
- 自己的 `stdout/stderr` 写出口

而用户线程做 syscall 时，
会先拿自己所属进程的那份工具箱。

所以这一步的真实意义不是“多了几行日志”，
而是：

> user process 第一次开始像一个真正拥有“内核视图”的对象。

---

## 6. 串口里现在会多看到什么

这一轮新增或变得更重要的标志包括：

- `user_mode_cwd=/`
- `user_mode_readme_prefix=os64fs readme`
- `user_mode_return_flags=0x0000000000000003`
- `user_thread_kernel_cwd_before=/docs`
- `user_thread_process_cwd_before=/`
- `user_thread_return_flags=0x0000000000000007`
- `user_thread_kernel_cwd_after=/docs`
- `user_thread_process_cwd_after=/`
- `user_thread_open_count=0`

如果这些都对，
说明两层都成立了：

1. ring 3 自己确实跑起来了
2. 它用到的 syscall 视图也已经开始按进程隔离

---

## 7. 这一轮之后，还没做什么

虽然这一轮已经比上一轮更像真正进程，
但它还远远不是完整用户子系统。

还没做的包括：

- `fork`
- `exec`
- `wait`
- ELF 用户程序加载
- 用户态页错误恢复
- timer 抢占后返回用户态
- 多条 user thread 共享同一进程地址空间
- 用户态阻塞后的正式恢复路径

所以这一步更准确的名字是：

```text
第一版 per-process syscall view
```

不是：

```text
完整用户进程模型
```

---

## 8. 这一轮之后，下一步最合理做什么

这一轮把“进程自己的 syscall 视图”立住以后，
下一步最合理的通常是继续把“用户线程被内核重新接回去”这件事做得更正式：

1. 这一步后来已经继续推进成“正式 `UserTrapFrame` + 每用户线程独立内核进入栈 + user yield/resume”，可以接着看 [KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md](./KERNEL_USER_TRAPFRAME_YIELD_GUIDE.md)
2. 再往后做更像样的用户程序加载器，而不是只靠内嵌 smoke program
3. 最后再去碰 `fork/exec/wait`

一句话记住这一轮：

```text
上一轮证明“user thread 已经能被 scheduler 管”
这一轮证明“这个 user process 已经开始有自己的 cwd/fd/syscall 视图”
```
