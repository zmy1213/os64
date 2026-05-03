# 从“正式 UserTrapFrame + user yield/resume”到“第一版 user timer preemption”

这一轮做的事情可以先用一句话概括：

```text
上一轮只证明了“用户线程可以主动 syscall -> yield -> 再回来”
这一轮继续证明“用户线程正在 ring 3 跑时，也能被 timer IRQ 抢占，再回到 ring 3 继续跑”
```

这两件事看起来都像“切走再回来”，
但其实不是一回事：

- `yield` 是用户线程自己主动发起的
- timer preemption 是外部时钟硬中断把它停下来的

后者更接近真正现代操作系统里的“抢占式调度”。

说明一下：

> 这篇文档讲的是“第一版 user timer preemption 刚刚跑通时”的思路。
> 当前仓库已经继续往前补上了“调度器保存/恢复内核上下文时也一起保存/恢复 CR3”。
> 那一轮请接着看 [KERNEL_SCHEDULER_CR3_SWITCH_GUIDE.md](./KERNEL_SCHEDULER_CR3_SWITCH_GUIDE.md)。

---

## 1. 这一步到底新证明了什么

现在 `os64` 里的 user thread 不只会：

1. 进入 ring 3
2. 用 `int 0x80` 打回内核
3. 在 syscall 里主动 `yield`
4. 再恢复回 ring 3

还会多一件更重要的事：

1. 用户线程正在 ring 3 自己跑
2. `PIT -> IRQ0 -> timer interrupt` 打进来
3. 内核保存这次“被抢占时”的完整寄存器现场
4. scheduler 切去另一条 helper thread
5. helper thread 跑完以后，再切回原来那条被打断的 user thread
6. 最后顺着原来的 IRQ 返回链 `iretq` 回 ring 3

也就是说，
这一步第一次让“真正的异步抢占”发生在用户态代码身上。

---

## 2. 为什么不能只靠上一轮的 `UserTrapFrame`

上一轮已经有 `UserTrapFrame` 了，
为什么这一轮还要继续改 IRQ 路径？

因为上一轮那份 trap frame 主要来自：

```text
ring3
-> int 0x80
-> syscall stub
-> C++ syscall handler
```

也就是“用户线程自己主动进内核”。

但 timer preemption 走的是另一条路：

```text
ring3
-> 外部 IRQ0
-> irq_stub_0
-> kernel_handle_irq()
```

如果 IRQ 路径只把最小 `vector/error/rip/cs/rflags` 交给 C++，
那内核就看不到：

- `r15/r14/.../rax`
- 被抢占时用户寄存器里到底装着什么
- 后面是不是能真的原样恢复

所以这一轮先做的第一件事就是：

```text
让 IRQ stub 和 syscall stub 一样，都把完整寄存器帧交给 C++
```

---

## 3. 这一轮为什么要把 IRQ 也升级成“完整寄存器帧”

现在 `kernel/interrupts/interrupts.hpp` 里多了一份：

```cpp
RegisterInterruptFrame
```

它本质上就是：

- 通用寄存器
- `vector`
- `error_code`
- `rip/cs/rflags`

按汇编压栈顺序原样排开的一个结构体。

这样做的原因很直接：

1. `int 0x80` 和 IRQ 终于用同一种“机器现场数据模型”
2. 以后想比较“主动 syscall 进内核”和“被 IRQ 抢占进内核”的差别，会非常清楚
3. C++ 代码可以正式把“用户线程被抢占时的寄存器值”保存起来

简单说：

```text
没有完整 IRQ 帧，就谈不上“观察用户态是怎么被抢占的”
```

---

## 4. 为什么要额外保存一份 `user_preempt_trap_frame`

这一步没有直接拿 timer IRQ 去覆盖已有的 `user_trap_frame`，
而是给 `ThreadControlBlock` 新增了：

- `has_user_preempt_trap_frame`
- `user_preempt_trap_frame`
- `user_preempt_count`

原因是教学上分开更清楚。

现在你可以这样理解：

- `user_trap_frame`
  更像“最近一次用户线程主动进内核”的现场
- `user_preempt_trap_frame`
  更像“最近一次用户线程正在跑时，被外部 timer 硬打断”的现场

这样看日志时不会混掉：

```text
这次是用户自己 int 0x80 进来的
还是 timer IRQ 把它强行停下来的
```

---

## 5. 为什么还要多映射一张共享页

这一轮在用户地址空间里多映射了一页：

```text
0x401000
```

它的作用不是放代码，
而是做一个非常小的“用户态 <-> helper thread”同步标志页。

里面只用了两个 64 位槽位：

1. `shared_page[0]`
   用户态把它置成 `1`，表示“我已经进入等待抢占的阶段了”
2. `shared_page[1]`
   helper thread 把它置成 `1`，表示“我确实在一次 timer 抢占后跑到了”

为什么要这样设计？

因为如果只是让用户程序“忙等一段时间”，
你并不能严格证明：

```text
它到底是真的被抢占过，
还是只是单纯空转了一会儿
```

共享页把这件事变成了可观察协议：

```text
用户态先 arm
-> timer 抢占发生
-> helper 被调度到
-> helper 写 done
-> 用户态继续往下跑
```

这比“猜一个循环次数”可靠得多。

---

## 6. helper thread 这次到底在干什么

这条 helper thread 不是为了做真实业务，
而是为了证明：

```text
user thread 被切走以后，系统里真的有“别的线程”拿到了 CPU
```

它现在的工作逻辑很小：

1. 第一次被调度到时，先记一次 `run_count`
2. 如果共享页还没 arm，就主动 `yield` 回去
3. 等以后某次 timer preemption 把它再次切上来
4. 看到共享页已经 arm，就把 done 置 `1`
5. 然后退出

注意这里有一个很容易忽略的点：

```text
helper 不能第一次跑完就直接结束
```

因为第一次跑到它，通常只是为了验证上一轮已经打通的：

```text
sys_yield -> helper -> 回 user thread
```

而第二次再跑到它，
才是为了验证这一步新增的：

```text
timer IRQ 抢占 user thread -> helper -> 再回 user thread
```

---

## 7. 这一轮最关键的一点：为什么“在 IRQ 返回链里切线程”也能工作

这一步最关键的现实点是：

```text
在“这一版 timer preemption 刚做出来时”，
教学内核还没有做完整的 CR3-aware 调度切换
```

也就是说，
它还不是那种“任意进程、任意线程都能在用户态被抢占，然后随便切去别的地址空间”的完整模型。

这一轮能先跑通，
靠的是两个保守前提：

1. helper thread 和 user thread 挂在同一个 user process 下面
2. helper thread 继续用低地址 identity-mapped 栈

这样即使 timer IRQ 是从 user CR3 打进来的，
切去 helper thread 时，
helper 仍然能看到：

- 内核代码
- 自己的 identity-mapped 栈
- 那张共享页对应的物理页

所以这一步还不是“最终形态”，
但它已经是一个真实里程碑：

```text
用户态被 IRQ 抢占
-> 暂停在内核 IRQ 返回链里
-> 切去别的线程
-> 再切回来
-> iretq 回 ring3
```

---

## 8. 现在串口里会看到哪些新日志

这一步跑通以后，
串口里你会多看到这些很关键的点：

- `user_mode_preempt_before=1`
- `user_mode_preempt_after=1`
- `user_thread_return_flags=0x000000000000000F`
- `user_thread_helper_preempt_signals=1`
- `user_thread_preempt_count=...`
- `user_thread_preempt_trap_vector=0x0000000000000020`
- `user_thread_preempt_trap_cs=0x0000000000000043`
- `user_thread_preempt_trap_ss=0x000000000000003B`

其中最值得你盯的是：

```text
user_thread_preempt_trap_vector = 0x20
```

因为 `0x20` 就是 PIC 重映射后的 `IRQ0`，
也就是 timer interrupt。

这说明保存下来的那份现场，
真的是“用户线程被时钟打断时”的现场，
不是 syscall 假装出来的。

---

## 9. 这一轮改了哪些文件

关键文件主要是：

- `kernel/interrupts/interrupts.hpp`
  增加通用的 `RegisterInterruptFrame`
- `kernel/interrupts/interrupt_stubs.asm`
  让 IRQ 也把完整寄存器帧交给 C++
- `kernel/interrupts/interrupts.cpp`
  在 timer IRQ 路径里捕获 user preempt trap frame，并在 IRQ 返回链上触发调度
- `kernel/task/scheduler.hpp`
  给 TCB 增加 `user_preempt_trap_frame` 和 `user_preempt_count`
- `kernel/task/context_switch.asm`
  扩展用户态 smoke program，增加 `preempt_before/after` 验证
- `kernel/core/kernel_main.cpp`
  增加共享页映射、helper thread 协议、新日志和新断言
- `scripts/test-stage1.sh`
- `scripts/test-page-fault.sh`
- `scripts/test-invalid-opcode.sh`
  把测试同步到新的用户态抢占行为

---

## 10. 这一步之后，下一步最合理做什么

这一步之后，
最合理的下一步通常不是马上去做文件系统扩展，
而是继续把“用户态调度”做得更正式：

1. 真正把 CR3 切换纳入 scheduler 上下文切换
2. 让 user thread 不只在“同一 user process + identity 栈 helper”这个保守模型下被抢占
3. 开始做更正式的用户栈/地址空间/用户程序装载
4. 再往后才是更像样的 `fork/exec/wait`

这件事在当前仓库里已经继续往前走了一步：

```text
调度器现在会连同暂停下来的内核栈一起保存/恢复对应的 CR3
```

所以如果你是按当前代码在读，
下一篇应该直接接：

- [从“第一版 user timer preemption”到“调度器保存内核上下文时也保存 CR3”](./KERNEL_SCHEDULER_CR3_SWITCH_GUIDE.md)

一句话记住这一轮：

```text
上一轮证明“用户线程可以主动 yield 再回来”
这一轮证明“用户线程正在 ring3 跑时，也会被 timer IRQ 抢占，再回到 ring3 继续跑”
```
