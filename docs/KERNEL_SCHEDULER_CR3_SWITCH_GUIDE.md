# 从“第一版 user timer preemption”到“调度器保存内核上下文时也保存 CR3”

这一轮解决的问题，不是“再多一次线程切换”这么简单，
而是把调度器从：

```text
只会保存下一次要恢复的 RSP
```

推进到：

```text
不仅保存 RSP，
还知道“这份暂停下来的内核上下文，下次恢复前应该先切到哪份 CR3”
```

如果你是小白，
先记一句最重要的话：

> 线程暂停时，保存下来的不只是“栈指针在哪”，还包括“这根栈属于哪份地址空间”。

---

## 1. 上一轮已经做到哪里了

上一轮已经证明了：

```text
user thread 正在 ring 3 跑
-> timer IRQ0 打进来
-> 暂停在 IRQ 返回链里
-> 切去 helper thread
-> 再切回来
-> iretq 回 ring 3
```

这已经非常接近真正现代内核里的“用户态被抢占”。

但是上一轮还有一个保守限制：

```text
helper thread 虽然也是 kernel thread，
但为了避免切错地址空间，
它只能先用低地址 identity-mapped 栈
```

这说明：

```text
调度器虽然会切线程，
但还没有完全理解“被暂停的内核上下文依赖哪份页表根”
```

这一轮就是把这个坑补上。

---

## 2. 为什么“只保存 saved_stack_pointer”不够

先想象一个最关键的场景：

1. 某条 user thread 正在 ring 3 运行
2. 它通过 `int 0x80` 进入内核
3. CPU 根据 `TSS.rsp0` 切到它自己的内核进入栈
4. 但此时 `CR3` 还是这条 user process 自己的页表根
5. 如果它在内核里 `yield` 或被抢占，调度器会把当前 `RSP` 保存下来

这时如果你只保存：

```text
saved_stack_pointer = 某个内核栈上的地址
```

还是不够。

原因是：

```text
这个 RSP 只是一个“虚拟地址”
```

而虚拟地址想变成真正能访问的内存，
必须放到正确的页表里解释。

也就是说：

- 同样一个 `RSP` 数值
- 在不同 `CR3` 下
- 看到的内存可能完全不一样

所以线程暂停时，
真正需要保存的是两件事：

1. 下次从哪里继续，也就是 `RSP`
2. 用哪份页表去理解这个 `RSP`，也就是 `CR3`

一句话说透：

```text
RSP 告诉你“回哪根栈”
CR3 告诉你“这根栈在哪个地址空间里才看得见”
```

---

## 3. 这里的 `CR3` 到底是什么

在 `x86_64` 里，
`CR3` 可以先粗略理解成：

```text
“当前 CPU 正在使用的页表根指针”
```

CPU 每次做虚拟地址翻译时，
都会从 `CR3` 指向的顶层页表开始往下走。

所以切换 `CR3` 的效果就是：

```text
同样一套寄存器值、同样一个虚拟地址，
CPU 会改用另一份地址空间去解释它
```

这就是为什么线程上下文切换不能只看寄存器，
还要看页表根。

---

## 4. 这一轮到底新增了什么结构

`ThreadControlBlock` 里现在多了一个字段：

```cpp
uint64_t saved_address_space_root_physical;
```

它保存的就是：

```text
这条线程当前这份“已暂停的内核上下文”，
下次恢复前应该先装进 CR3 的那份页表根物理地址
```

你可以把它理解成：

- `saved_stack_pointer`
  说明“上次停在这根栈的哪里”
- `saved_address_space_root_physical`
  说明“恢复这根栈之前先切回哪份世界地图”

这样调度器就不再只是“搬栈指针”，
而是真的开始保存：

```text
线程暂停时的执行环境
```

---

## 5. 为什么“暂停下来的内核上下文”会依赖不同的根页表

这一步最容易绕晕，
所以直接拆成 3 种情况看。

### 情况 A：还没真正跑起来过的新线程

比如：

- 新建的 kernel thread
- 新建的 user thread

它们现在只有一份“初始内核栈伪造帧”，
还没有真正执行过，
所以也还没有“暂停在某份特殊地址空间里”的历史。

这时最合理的默认值就是：

```text
先按 kernel root 恢复
```

原因很简单：

1. kernel thread 本来就应该先在内核地址空间里启动
2. user thread 第一次也不是直接落到 ring 3
3. 它会先在 kernel root 下跑 `scheduler_thread_bootstrap()`
4. 再由 `user_mode_enter()` 自己切去 user root

所以新线程刚创建时，
`saved_address_space_root_physical` 先初始化成 kernel root。

---

### 情况 B：user thread 已经从 ring 3 进过一次内核，然后在内核里被暂停

这是这一轮最关键的真实情况。

例如：

```text
ring 3 user code
-> int 0x80
-> CPU 切到 TSS.rsp0 那根内核进入栈
-> kernel_handle_syscall()
-> 线程在内核里 yield / block / 被切走
```

注意这里有一个非常重要的事实：

```text
CPU 从 ring 3 进 ring 0 时，会切栈，但不会自动切 CR3
```

也就是说，
这段 syscall 内核路径仍然跑在：

```text
当前 user process 自己的页表根
```

下面。

所以如果这时线程被暂停，
调度器保存下来的内核栈 `RSP`，
其实是“依赖 user root 才能正确解释”的。

那下次恢复它时，
顺序就必须是：

1. 先把这条线程上次暂停时的 root 装回 `CR3`
2. 再把 `RSP` 恢复成它上次停下来的值

不然 CPU 虽然拿到了同一个 `RSP`，
却可能在错误的地址空间里读写那根栈。

---

### 情况 C：kernel helper thread 虽然挂在 user process 下面，但它本质上还是 kernel thread

这也是这一轮最现实的收益。

以前为了让这条 helper thread 在“可能还停留在 user root 的环境里”也能安全运行，
最保守的办法是：

```text
给它一根低地址 identity-mapped 栈
```

因为这种栈无论在 kernel root 还是 user root 下，
通常都还能看见。

但这不是一个漂亮的长期方案。

现在调度器已经知道：

```text
helper thread 自己保存下来的上下文，
本来就应该在 kernel root 下恢复
```

所以它恢复时会先切回 kernel root，
再加载自己的内核栈。

这样一来，
helper thread 就可以重新用普通 `kernel heap` 栈，
不需要再“委屈自己”去挤低地址 identity 区。

这比上一轮更接近真实内核：

```text
即使线程 owner 是 user process，
这条线程只要本质上是 kernel thread，
它就应该按 kernel thread 的方式恢复
```

---

## 6. 为什么汇编里的顺序必须是“先保存当前 RSP，再切 CR3，再加载下一个 RSP”

`scheduler_switch_context_and_root()` 这一轮最核心的地方不是“多写了一条 `mov cr3, rax`”，
而是：

```text
这条指令必须放在对的顺序里
```

正确顺序是：

1. 先把当前线程的 callee-saved 寄存器压栈
2. 在当前还有效的地址空间里，把当前 `RSP` 写回 `saved_stack_pointer`
3. 再把“下一条线程需要的 root”写进 `CR3`
4. 最后把 `RSP` 切到下一条线程自己的保存值
5. 再从那根栈里 `pop` 出寄存器并 `ret`

为什么不能把 `mov cr3, ...` 放前面？

因为如果你先切了 `CR3`，
再去写：

```text
[rdi] = rsp
```

那 `rdi` 指向的保存槽位本身就可能已经不在当前地址空间里了。

结果会变成：

- 要么写错地方
- 要么直接 page fault
- 要么把当前线程最后这份上下文保存坏

所以这一步的核心不是“会切 root”，
而是：

```text
在切 root 前，先把当前世界里的最后一点状态存干净
```

---

## 7. 这一步调度器代码是怎么组织的

这一轮在 `kernel/task/scheduler.cpp` 里，核心多了 4 个小角色：

### `scheduler_kernel_root_physical()`

它负责回答一个问题：

```text
当前系统里，kernel root 到底是谁
```

优先取 scheduler 里 0 号 kernel process 的地址空间根；
如果那边还没就绪，
再退回当前 `paging_current_root_physical()`。

---

### `thread_resume_root_physical()`

它负责回答：

```text
这条线程下次恢复前，到底该先切到哪份 root
```

逻辑是：

1. 如果线程已经保存过自己的 `saved_address_space_root_physical`
   就直接用它
2. 如果这是还没跑起来的新线程
   就先回到 kernel root

这样做的意义是：

```text
新线程和暂停线程，终于被清楚地区分开了
```

---

### `initialize_thread_saved_root()`

它在新线程刚创建时调用，
先把线程的 `saved_address_space_root_physical` 设成 kernel root。

这样第一轮恢复它时，
调度器就不会拿到一个空 root。

---

### `switch_thread_context()` / `switch_from_bootstrap_to_thread()` / `switch_from_thread_to_bootstrap()`

这 3 个入口把“谁切到谁”分开写清楚了：

1. 线程切线程
2. bootstrap 栈第一次切进线程
3. 线程退出后切回 bootstrap 栈

它们共同做的事是：

- 先确定当前线程暂停时该保存哪份 root
- 再确定下一份上下文该用哪份 root 恢复
- 最后统一走 `scheduler_switch_context_and_root()`

这样调度器代码终于明确表达出：

```text
上下文切换 = 栈切换 + 地址空间根切换
```

---

## 8. 这一步为什么能让 helper kernel thread 回到普通 heap 栈

这一轮很重要的一个外部表现是：

```text
scheduler_create_kernel_thread()
```

不再对“挂在 user process 下面的 kernel thread”做特殊处理了。

现在它直接：

```text
kmalloc_aligned(stack_bytes, 16)
```

分普通内核堆栈。

这是因为现在的恢复语义已经变成：

```text
helper thread 上次如果是在 kernel root 下暂停的，
那它下次恢复前就会先切回 kernel root
```

所以它自然可以重新看见：

- 自己的 heap 栈
- 普通内核堆对象
- 正常内核虚拟地址区

这一步本质上是在告诉你：

```text
之前不是 heap 栈“天生不能用”，
而是调度器当时还没学会先恢复正确的地址空间
```

---

## 9. 现在串口日志里最值得你盯哪几个点

这一步之后，
除了上一轮的 `yield` / `timer preempt` 标志，
还多了 3 个很关键的观测点：

- `user_thread_kernel_root=0x...`
- `user_thread_helper_stack_base=0x...`
- `user_thread_helper_resume_root=0x...`

它们组合起来要证明的是：

### 证明 1：helper thread 现在确实不再用低地址 identity 栈

如果 `user_thread_helper_stack_base` 已经明显高于：

```text
kPagingBootIdentityLimit
```

那就说明它拿到的是普通 heap 栈，
不是早期那个“为了保命先塞到低地址”的过渡方案。

### 证明 2：helper thread 恢复前会回到 kernel root

如果：

```text
user_thread_helper_resume_root == user_thread_kernel_root
```

就说明调度器保存和恢复的 root 逻辑已经生效了。

### 证明 3：在这些变化之后，旧链路仍然没坏

也就是这些老日志还继续成立：

- `user_mode_yield_before=1`
- `user_mode_yield_after=1`
- `user_mode_preempt_before=1`
- `user_mode_preempt_after=1`
- `user_thread_preempt_trap_vector=0x0000000000000020`

这说明：

```text
我们不是“换了一种启动方式”，
而是在原有用户态 yield / preempt 链路上，
把地址空间切换补得更正式了
```

---

## 10. 这一轮改了哪些关键文件

主要是这些：

- `kernel/task/scheduler.hpp`
  给 `ThreadControlBlock` 增加 `saved_address_space_root_physical`
- `kernel/task/context_switch.asm`
  增加 `scheduler_switch_context_and_root()`
- `kernel/task/scheduler.cpp`
  把线程恢复 root、保存 root、bootstrap/root 切换逻辑接起来
- `kernel/core/kernel_main.cpp`
  增加新的串口日志和断言，证明 helper stack / helper resume root 都正确
- `scripts/test-stage1.sh`
- `scripts/test-page-fault.sh`
- `scripts/test-invalid-opcode.sh`
  把新的观测日志也纳入测试

---

## 11. 这一轮和上一轮 user timer preemption 的关系

你可以把这两轮理解成：

### 上一轮

先证明：

```text
用户态正在跑
-> timer IRQ 真能把它打断
-> 还能切去别的线程再回来
```

### 这一轮

再把里面原来比较保守的过渡方案补正式：

```text
线程暂停时不只保存 RSP，
还保存“恢复这份内核上下文前该先切到哪份 CR3”
```

所以这一轮不是推翻上一轮，
而是把上一轮从：

```text
能跑通
```

推进到：

```text
为什么能跑通，现在终于有了更像样的地址空间语义
```

---

## 12. 这一步之后，下一步最合理做什么

这一轮补完以后，
最合理的下一步通常不是马上做文件系统，
而是继续把“用户态执行模型”补得更完整：

1. 先把 ring3 里的阻塞式 syscall 真正跑通，比如 `read(0)` 空输入时 block，再由键盘 IRQ 唤醒
2. 把“保存下来的用户态现场”和“恢复用户态现场”进一步做成更正式的通用机制
3. 开始做更像样的用户程序装载，而不只是内建 smoke program
4. 再往后才是更正式的 `exec`、独立用户镜像、甚至 `fork`

当前仓库里，第一步已经继续往前做了：

- [从“调度器保存内核上下文时也保存 CR3”到“用户态 `read(0)` 真正 block/wake”](./KERNEL_USER_STDIN_BLOCK_GUIDE.md)

一句话记住这一轮：

```text
上一轮证明“用户线程会被 IRQ 抢占”
这一轮证明“调度器已经开始真正理解：暂停下来的内核栈属于哪份 CR3”
```
