# 从第一版 `process/thread/scheduler` 到“shell 真正跑进调度器”

这一轮做的不是“再加一个命令”，
而是把前一轮刚搭出来的 `process/thread/scheduler` 真正接到用户能看见的交互路径上。

一句话先说结论：

```text
以前：kernel_main 自己直接跑 shell
现在：kernel_main 创建 shell 线程，再交给 scheduler
```

同时还补了另一半：

```text
以前：console 等输入时主要靠 hlt + 醒来重试
现在：console 优先走 keyboard wait queue -> block -> IRQ 唤醒
```

这两件事必须一起做，
不然 shell 虽然“名义上在线程里”，
但它等输入时仍然会被错误地算成一直在占 CPU。

---

## 1. 为什么这一轮必须做

上一轮你已经有了：

- `ProcessControlBlock`
- `ThreadControlBlock`
- `SchedulerState`
- ready queue
- idle thread
- `sleep/block/wake`

但当时真正的交互入口仍然是：

```text
kernel_main()
  -> 初始化
  -> shell_run_forever()
```

这说明：

```text
调度器已经存在
但真实系统入口还没有交给调度器
```

所以当时的任务系统更像：

```text
“会做 smoke test 的调度器骨架”
```

而不是：

```text
“真的接管内核交互路径的调度器”
```

这一步就是把这层差距补上。

---

## 2. 为什么不能只改 `kernel_main`

很多人会第一反应写成：

```text
create shell thread
scheduler_run_until_idle()
```

看起来像是已经完成了。

但这里有个关键坑：

`console_read_line_with_history()` 以前在没输入时主要做的是：

```text
while (!有输入) {
  hlt
  醒来后重试
}
```

这在“还没有线程模型”的时候没问题，
因为整个系统本来就只有一条主线。

可是一旦 shell 已经变成线程，
这个行为就不够好了。

原因是：

### 2.1 它没有真的进入 blocked

也就是说调度器看起来像是：

```text
shell 线程还在 running
```

但从逻辑上说，
它其实只是：

```text
正在等键盘输入
```

这应该属于 `blocked`，
而不是继续算它“正在跑”。

### 2.2 idle thread 也接不上

如果 shell 只是在自己的循环里 `hlt`，
那“没有别的 ready 线程时 CPU 应该切给 idle thread”这条链路就不会被真实交互路径用起来。

所以这一轮必须把：

```text
shell 线程化
+ console 输入阻塞化
```

一起做。

---

## 3. 这一轮具体改了什么

### 3.1 `keyboard` 增加“通用输入事件等待队列”

之前已经有：

- `keyboard_wait_for_stream_char()`

它服务的是：

```text
stdin/read(0)
```

因为 `stdin` 只关心字符流，
方向键这些编辑事件本来就会被忽略。

但 `console_read_line_with_history()` 不一样，
它还关心：

- `ArrowLeft`
- `ArrowRight`
- `ArrowUp`
- `ArrowDown`
- `Home`
- `End`
- `Delete`

所以这一轮又补了：

- `keyboard_wait_for_input_event()`

它等的是：

```text
任意完整输入事件
```

这正好适合 console 行编辑器。

### 3.2 `console_read_line_with_history()` 先尝试真正 block

现在它在没有输入事件时，
顺序变成：

1. 先尝试 `keyboard_wait_for_input_event()`
2. 如果当前确实在线程上下文里，就把当前线程挂进等待队列
3. 由下一次键盘 IRQ 来唤醒它
4. 只有在“当前不在线程上下文”或“暂时不能 block”时，才退回旧的 `hlt` 路径

这样做的好处是：

```text
真实交互 shell 会走 block/wake
早期 smoke test 仍然可以继续工作
```

因为有些 smoke test 还不是在线程上下文里直接跑的，
所以这里必须保留一个安全回退路径。

### 3.3 `shell` 抽出 `shell_run_once()`

以前：

```text
shell_run_forever()
  -> 打提示符
  -> 读一行
  -> 执行命令
  -> 无限循环
```

现在先抽出：

- `shell_run_once()`

它专门负责“一轮真实 shell 交互”：

1. 打提示符
2. 读一行
3. 执行命令

这样做的原因是：

```text
真实交互路径和 smoke test 可以复用同一条逻辑
```

否则很容易出现：

```text
测试走一套代码
真实启动又走另一套代码
```

最后你会看到“测试全绿，但真正启动路径有问题”。

### 3.4 `kernel_main` 不再自己直接跑 shell

现在正常启动路径会做成：

```text
initialize_shell(...)
enable_interrupts()
create process "kernel-shell"
create thread  "shell-main"
scheduler_run_until_idle(&g_scheduler)
```

注意这里的 `scheduler_run_until_idle()` 对交互 shell 来说几乎不会返回，
因为：

```text
shell-main 是一个长期驻留线程
```

这正是我们想要的行为：

> 从这里开始，不再是 `kernel_main` 自己主持交互，而是调度器接管系统主线。

### 3.5 真实交互前重新初始化 shell

前面的 shell smoke test 会故意执行很多命令，
包括改 `cwd`、写历史记录。

所以这一轮又在真正进入交互前重新跑一次：

- `initialize_shell(...)`

原因很简单：

```text
测试态和真实交互态要分开
```

否则你刚启动完系统，
一进 shell 看到的 history 和 cwd 就已经被测试污染了。

---

## 4. 这一轮新的关键日志代表什么

正常启动测试里现在会额外看到：

```text
shell_process_pid=5
shell_thread_tid=11
shell_thread_started_pid=5
shell_thread_started_tid=11
```

它们分别说明：

- shell 进程对象真的创建出来了
- shell 线程对象真的创建出来了
- 调度器真的切进了 shell 线程
- shell 线程里看到的 PID/TID 和创建时一致

也就是说现在验证的不是：

```text
“shell 代码存在”
```

而是：

```text
“shell 已经作为真正线程被 scheduler 接管”
```

---

## 5. 这一轮你应该从代码里重点看哪几处

如果你想按最小主线看源码，
建议顺序是：

1. `kernel/interrupts/keyboard.hpp/.cpp`
   看 `keyboard_wait_for_input_event()` 和 IRQ 唤醒等待者。
2. `kernel/console/console.cpp`
   看 `console_read_line_with_history()` 怎么先尝试 block，再回退到旧路径。
3. `kernel/shell/shell.cpp`
   看 `shell_run_once()` 和 `shell_run_forever()` 的关系。
4. `kernel/core/kernel_main.cpp`
   看 `start_kernel_shell_under_scheduler()` 和 `kernel_shell_thread_entry()`。

这样看最容易把“等待输入 -> 被唤醒 -> 继续读行 -> 执行命令 -> 再次等待”这条链串起来。

---

## 6. 怎么测试这一轮

正常启动回归：

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

注意这几个测试都共用：

```text
build/disk.img
```

所以还是按顺序跑，不要并行跑。

---

## 7. 现在这个系统和“现代内核”还差什么

这一轮虽然已经让 shell 真正进了 scheduler，
但它仍然是：

```text
内核线程
```

不是：

```text
ring 3 用户进程
```

所以后面还缺的关键层至少有：

- TSS
- ring 3 栈切换
- 每进程地址空间
- 用户程序装载
- 真正的抢占
- 锁和中断同步

这一轮结束后，最顺的下一步就是先把 `TSS` 补上。

现在这一部分已经单独接到了下一篇：

- [从“shell 真正跑进调度器”到第一版 `TSS`](./KERNEL_TSS_GUIDE.md)

但这一步非常值得，
因为现在“用户能看到的交互入口”已经和任务系统站在同一条主线上了。

一句话总结：

> 这一步不是让 shell 更花哨，而是让调度器第一次真正接管了系统的长期执行主线。
