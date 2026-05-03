# 从“调度器保存内核上下文时也保存 CR3”到“用户态 `read(0)` 真正 block/wake”

这一轮做的事情可以先压成一句话：

```text
用户线程现在不只会在 ring3 被 timer IRQ 抢占，
还会在 ring3 里调用 `read(0)` 时真正 block，
然后再被键盘 IRQ 唤醒回来
```

如果你是小白，
这一轮最重要的理解是：

> 抢占只是“外部把你打断”；阻塞 syscall 更难，因为它要求线程主动在内核里睡下去，然后以后再从同一个 syscall 里醒过来。

---

## 1. 上一轮已经做到哪里了

上一轮已经补上了：

```text
线程切换时不只保存 RSP，
还会保存/恢复这份暂停下来的内核上下文对应的 CR3
```

这意味着：

- user thread 如果在 user root 下暂停
  下次恢复前会先回到它自己的 user root
- kernel helper thread 如果本来就该跑在 kernel root
  下次恢复前会先回到 kernel root

这一步已经让：

```text
user yield / timer preempt / helper kernel thread
```

这些链路变得更正式。

但还有一个更像真实操作系统的问题没证明：

```text
如果用户线程在 ring3 里调 `read(0)`，
而当前又没有字符，
它能不能真的先 block，
再由键盘 IRQ 把它唤醒？
```

这一轮就是在补这件事。

---

## 2. 为什么这一步比“timer preempt 再回来”更难

`timer preempt` 的特点是：

```text
用户线程正在跑
-> 外部 IRQ 强行把它打断
```

它不需要用户线程自己决定要不要睡。

但 `read(0)` 空输入不是这样。

这里的链路是：

```text
用户线程自己主动发起 syscall
-> 内核发现“现在没有字符”
-> 内核把当前线程挂进 keyboard wait queue
-> 当前线程真的进入 blocked
-> 以后某次键盘 IRQ 到来再把它唤醒
-> 最后从原来那次 `read(0)` 返回
```

也就是说，
这里要求的是：

1. 线程先从 ring3 主动进内核
2. 在 syscall 里睡下去
3. 中间调度器要切给别人
4. 以后再回到同一条 syscall 执行流
5. 最后再回 ring3

这比“被中断打一下再回来”要更像真正的阻塞式系统调用。

---

## 3. 这一轮先撞上的真正坑是什么

最大的坑其实不是 wait queue，
而是：

```text
`int 0x80` 现在走的是 interrupt gate
```

在 `x86` 里，
interrupt gate 有一个非常关键的行为：

```text
CPU 进门时会自动把 IF 清掉
```

也就是：

```text
用户态本来开着中断
-> 一旦执行 `int 0x80`
-> 内核刚接到 syscall 时，IF 已经被 CPU 先关掉了
```

这会直接影响 `read(0)` 这类阻塞 syscall。

因为当前 `read_stdin_stream()` 的逻辑是：

1. 先尝试直接读字符
2. 如果没有字符，再看“当前能不能等中断”
3. 如果中断根本没开，就不能安全睡下去等 IRQ

所以如果不补任何东西，
用户态 `read(0)` 就永远不可能真正 block 等键盘 IRQ。

---

## 4. 所以这一轮为什么要在用户态 syscall 分发期间重新开中断

这一步没有粗暴地把所有 `int 0x80` 都改成 trap gate，
而是先做了一个更保守的版本：

```text
只有当这次 syscall 确实来自 ring3，
并且用户态原来的 RFLAGS 里 IF 本来就是开的，
内核才在真正分发 syscall 期间重新开中断
```

也就是说：

- 内核态自己做的 `int 0x80` 测试
  行为不变
- 用户态真正想等待外部 IRQ 的 syscall
  才获得“可以阻塞等待”的条件

你可以把这一步理解成：

```text
CPU 帮我们进门时先把灯关了
但这条 syscall 本来就需要等门外有人敲门
所以内核在确认安全后，再把灯重新打开
```

这里的“灯”就是 `IF`。

---

## 5. 这一轮用户态 smoke program 新做了什么

之前那段用户态测试程序已经会：

1. `getcwd`
2. `open/read/close("readme.txt")`
3. `yield`
4. 等 timer preempt

这一轮它最后又追加了一段：

```text
打印 user_mode_stdin_before=1
-> 把共享页里的 stdin arm 标志置 1
-> 调 `read(0, buf, 1)`
-> 等回来后检查：
   - 返回值是不是 1
   - 读到的字符是不是 'a'
-> 成功就打印 user_mode_stdin_after=1
```

所以现在这条用户态程序已经连续验证了 4 类事情：

1. 用户态能看自己的 cwd
2. 用户态能走自己的相对路径/文件描述符视图
3. 用户态能主动 yield，再回来
4. 用户态既会被 timer IRQ 抢占，也会在 `read(0)` 里 block，再被键盘 IRQ 唤醒

---

## 6. helper thread 这一轮为什么还要继续升级

这条 helper thread 之前主要负责证明：

```text
用户线程被切走以后，真的有别的线程拿到了 CPU
```

现在它又多了第二个职责：

```text
看到用户线程已经把“stdin 阶段” arm 起来以后，
先确认目标 user thread 真的已经进入 blocked，
再注入一个键盘扫描码把它唤醒
```

这里特意不是“看到 arm 就立刻注入”。

原因是如果这样写，
就可能退化成：

```text
字符比 block 更早到
-> read(0) 直接读到字符
-> 虽然功能看起来成功，但你根本没证明它真的 block 过
```

所以 helper 现在会先检查：

```text
wake_target->state == blocked
```

只有确认用户线程已经真的睡下去，
它才继续等 1 个 tick，然后注入测试扫描码。

这一步非常关键，
因为它把“用户态读 stdin”从“可能只是碰巧读到一个现成字符”，
提升成了：

```text
明确发生过 block -> wake 这条调度链
```

---

## 7. 这一轮完整链路到底怎么走

现在完整流程可以按下面读：

```text
ring3 user code
-> int 0x80 read(0)
-> kernel_handle_syscall()
-> 因为来自 ring3 且用户原本开着 IF，所以先重新开中断
-> sys_read()
-> read_stdin_stream()
-> keyboard_wait_for_stream_char()
-> register current thread into keyboard stream wait queue
-> scheduler_block_current_thread_and_enable_interrupts()
-> 切去 helper thread
-> helper 看到目标 user thread 已经 blocked
-> helper 等 1 个 tick
-> keyboard_inject_test_scancode('a')
-> 键盘 IRQ 到来
-> keyboard IRQ 把 blocked 的 user thread wake 成 ready
-> helper 退出
-> scheduler 再切回原来那条 user thread
-> 从原来那次 `read(0)` 继续执行
-> 返回 1 个字节
-> ring3 打印 user_mode_stdin_after=1
```

这一轮真正新证明的点是：

```text
用户线程不只是“会被打断”
而是“能在 syscall 里真的睡下去，然后以后从同一个 syscall 里醒过来”
```

---

## 8. 为什么这一轮还要继续复用那张共享页

之前那张共享页已经有两个槽位：

1. `shared_page[0]`
   用户态告诉 helper：“我已经进入等 timer preempt 的阶段了”
2. `shared_page[1]`
   helper 告诉用户态：“你确实已经被抢占过了”

这一轮继续往后扩成了 4 个槽位：

3. `shared_page[2]`
   用户态告诉 helper：“我马上要进入 stdin block 阶段了”
4. `shared_page[3]`
   helper 告诉内核/测试：“我已经完成了唤醒用的键盘注入”

这样设计的好处是：

```text
抢占协议
和
阻塞唤醒协议
都可以在同一条 user thread smoke 里被清楚观察到
```

---

## 9. 现在串口里最值得盯哪几行

这一步成功以后，
最重要的新日志是：

- `user_mode_stdin_before=1`
- `user_mode_stdin_after=1`
- `user_thread_return_flags=0x000000000000001F`
- `user_thread_stdin_arm_flag=1`
- `user_thread_stdin_done_flag=1`
- `user_thread_helper_stdin_signals=1`
- `user_thread_helper_block_observed=1`

这些日志一起说明：

### 证明 1：用户态代码真的走到了 stdin 阶段

`user_mode_stdin_before=1`

### 证明 2：它不是挂死在 `read(0)` 里，而是真的回来了

`user_mode_stdin_after=1`

### 证明 3：helper 不是“乱打一针”，而是在看到 blocked 以后才完成唤醒

`user_thread_helper_block_observed=1`

### 证明 4：最终自检标志从 `0xF` 变成了 `0x1F`

也就是：

```text
原来的 cwd/readme/yield/preempt 都还成立
+ 新增的 stdin block/wake 也成立
```

---

## 10. 这一轮改了哪些关键文件

主要是：

- `kernel/syscall/syscall.cpp`
  在“来自 ring3 且用户原本开着 IF”的前提下，让 syscall 分发期间重新开中断
- `kernel/task/context_switch.asm`
  扩展用户态 smoke program，加入 `read(0)` block/wake 验证
- `kernel/core/kernel_main.cpp`
  扩展 helper thread 协议、增加新的串口日志和断言
- `scripts/test-stage1.sh`
- `scripts/test-page-fault.sh`
- `scripts/test-invalid-opcode.sh`
  把新的用户态 stdin block/wake 里程碑纳入回归

---

## 11. 这一步之后，下一步最合理做什么

这一步之后，
最合理的下一步通常不是再多堆几个 syscall，
而是把“用户程序”本身做得更正式：

1. 把现在内建在内核里的用户态 smoke program，推进成“从文件系统装载的用户程序”
2. 给用户程序准备更清楚的入口格式，而不是简单复制一段裸汇编页
3. 再往后才更适合做 `exec`、更独立的用户镜像、甚至 `fork`

一句话记住这一轮：

```text
上一轮证明“调度器知道暂停下来的内核栈属于哪份 CR3”
这一轮证明“用户线程在 ring3 的 `read(0)` 里真的会 block，再被键盘 IRQ 唤醒回来”
```
