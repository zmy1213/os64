# 从“每进程地址空间骨架”到“第一次真正进入用户态”

这一轮终于第一次真正碰到：

```text
ring 3 用户态
```

但这里做的还不是：

- ELF 用户程序加载器
- 真正的用户进程生命周期
- `syscall/sysret`
- 抢占式用户线程调度

这一轮先只做一个最小而真实的闭环：

> 内核手工准备一份用户态入口现场，用 `iretq` 真正落进 ring 3，再让用户代码用现有 `int 0x80` 回到内核。

一句话先说结论：

```text
以前：int 0x80 只是“内核里自己打一下”的软中断入口
现在：已经真的能从 ring 3 执行用户代码，再从用户态打 int 0x80 回 ring 0
```

这一步的意义非常大，因为它第一次证明了：

```text
这个 64 位内核不只是“像有 syscall”
而是真的已经具备 ring 3 <-> ring 0 切换能力
```

---

## 1. 为什么先做这个，而不是先做文件系统/更多 shell 命令

前一轮你已经补了两层很关键的基础设施：

- `TSS`
- `AddressSpace`

这两层补完以后，最自然的下一步就不是继续堆 shell 功能，
而是把它们真正“用起来”。

因为这两层本来就是为下面这件事服务的：

```text
让 CPU 从用户态安全进入内核态
```

如果还不做一次真的 ring 3 进入，
那前一轮很多结构仍然只是“准备好了，但还没被证明真的工作”。

所以这一轮选的目标非常聚焦：

1. 准备一份合法的用户地址空间
2. 准备一份合法的用户代码页和用户栈页
3. 准备 `iretq` 需要的 5 个关键值
4. 真正进入 ring 3
5. 从 ring 3 执行 `int 0x80`
6. 再安全回到当前内核测试流程

---

## 2. 这一步到底在解决什么问题

前面虽然已经有：

- `int 0x80`
- `SyscallContext`
- `sys_open/sys_read/sys_write/...`

但那时的 `int 0x80` 其实还是：

```text
“内核态代码自己故意打一个软中断，看看 IDT 和分发器通不通”
```

它还不能证明一件更重要的事：

```text
如果 CPU 当前真的跑在 CPL=3，
它能不能：
1. 用用户段和用户栈执行代码
2. 通过中断门切回 CPL=0
3. 在内核处理完以后继续收尾
```

这一步就是把这个缺口补上。

---

## 3. 为什么 `TSS` 一定要在前一轮先补

这是这一轮最重要的前置知识。

如果 CPU 正在 ring 3 跑用户代码，
然后发生：

- `int 0x80`
- page fault
- general protection fault
- 外部中断

而目标门描述符的特权级更高，
CPU 就必须先完成一件事：

```text
把当前栈从“用户栈”切到“内核栈”
```

问题是：

```text
切到哪根内核栈？
```

这个答案不在普通 C++ 变量里，
而是在：

```text
TSS.rsp0
```

里。

所以没有前一轮那份 `TSS`，
这一轮就算你硬凑出 `iretq` 进了 ring 3，
用户态第一次打回内核时也没有一条正式、CPU 认可的内核栈切换路径。

一句话记：

```text
TSS 先解决“从用户态回来时站在哪根内核栈上”
```

---

## 4. 为什么 `AddressSpace` 也要在前一轮先补

另一个必须先回答的问题是：

```text
这个“用户程序”到底活在哪份页表里？
```

如果没有 `AddressSpace`，
后面这些都没地方落：

- 用户代码页映射到哪
- 用户栈页映射到哪
- 进入用户态前要把 `CR3` 切成哪一份根页表

所以这一轮不是直接在“当前全局内核页表”上胡乱挂页，
而是先：

```text
clone 当前地址空间
```

得到一份带内核基础映射的独立根页表，
再往这份副本里额外挂：

- 用户代码页
- 用户栈页

这样以后真正切 `CR3` 时，CPU 才能在这份地址空间里同时看到：

- 仍然可用的内核基础映射
- 新增的用户空间页

---

## 5. 这一轮具体改了什么

主要涉及这些文件：

- `kernel/boot/segments.hpp`
- `kernel/interrupts/interrupts.cpp`
- `kernel/memory/paging.cpp`
- `kernel/core/kernel_main.cpp`
- `kernel/task/context_switch.asm`
- `kernel/syscall/syscall.hpp`
- `kernel/syscall/syscall.cpp`
- `scripts/test-stage1.sh`
- `scripts/test-page-fault.sh`
- `scripts/test-invalid-opcode.sh`

你可以把它们分成 4 组来理解：

### 5.1 用户段选择子

在 `kernel/boot/segments.hpp` 里新增了：

- `kUserDataSelector = 0x38`
- `kUserCodeSelector = 0x40`
- `kUserDataSelectorRpl3 = 0x3B`
- `kUserCodeSelectorRpl3 = 0x43`

作用是：

```text
第一次 iretq 进入 ring 3 时，
CPU 必须看到合法的用户代码段和用户数据段选择子
```

注意这里故意把用户段放在 TSS 描述符后面，
这样原来的：

```text
kKernelTssSelector = 0x28
```

不用变。

这很重要，因为前一轮很多测试和 `ltr` 都已经依赖这个值。

---

### 5.2 内核自己的 GDT 现在多了 ring 3 段

在 `kernel/interrupts/interrupts.cpp` 里，
内核自己维护的 `g_kernel_gdt` 从原来的 7 个 qword 扩成了 9 个 qword。

新增的是：

- user data descriptor
- user code descriptor

为什么一定要补？

因为 `iretq` 不是“想跳哪里就跳哪里”，
它会严格检查你准备的：

- `CS`
- `SS`

对应的段描述符是否合法、DPL 是否匹配。

如果没有 ring 3 可用段，
第一次进入用户态就会直接出异常。

---

### 5.3 `paging` 现在不只要求叶子 PTE 有 `U=1`

这是这一轮最关键的实现坑之一。

一开始很容易以为：

```text
只要最后那一条 PTE 是 user page，用户态就能访问
```

其实不对。

x86_64 在做用户态访问检查时，
不只会看最后一级页表项，
还会一路检查上层：

- PML4E
- PDPTE
- PDE
- PTE

如果其中任何一级还是 supervisor-only，
用户态访问仍然会 fault。

所以这轮在 `kernel/memory/paging.cpp` 里把 `ensure_next_level()` 扩成了：

```text
向下创建或复用页表层级时，
把需要的 U/W 标志一路传播到父页表项
```

这就是为什么现在 `map_page_in_root()` 不只会给叶子页设标志，
还会保证父级表项在需要时也带上 `kPageUser`。

如果不做这一步，
第一次用户态取指就会 page fault。

---

### 5.4 新增第一版 `user_mode_enter` / `user_mode_resume_kernel`

这套最核心的汇编放在：

```text
kernel/task/context_switch.asm
```

之所以复用这个文件，
而不是新建一个额外 asm 文件，
是为了不把构建链再弄复杂一层。

这里新增了两条关键路径：

#### `user_mode_enter`

它做的事情是：

1. 先保存当前内核调用点需要保住的 callee-saved 寄存器
2. 把“以后恢复时该回到哪根内核栈”保存进 `UserModeSession`
3. 把 `CR3` 切到目标用户地址空间根页表
4. 手工压出 `iretq` 需要的 5 项
5. 执行 `iretq`

这里最关键的是你要记住：

```text
从 ring 0 切到 ring 3 的 iretq 帧，需要这 5 个值：
SS
RSP
RFLAGS
CS
RIP
```

压栈顺序正好和 `iretq` 弹出的顺序相反。

也就是说代码里先 `push SS`，
最后 `push RIP`，
然后 `iretq` 一执行，
CPU 就会按规则把这些值装回去。

#### `user_mode_resume_kernel`

它做的事情是：

1. 把 `CR3` 切回原来的内核页表根
2. 把 `DS/ES/FS/GS` 恢复成内核数据段
3. 恢复最初 `user_mode_enter()` 压下去的 callee-saved 寄存器
4. `ret`

这里的关键点是：

```text
它不是回到自己的调用者，
而是直接把控制流“接回 user_mode_enter 的调用点”
```

所以这一轮它本质上是一个：

```text
教学版、一次性的非本地返回器
```

这很适合 smoke test，
但以后做真正进程退出时，
应该升级成正式的线程/进程退出路径。

---

## 6. `UserModeSession` 是什么

在 `kernel/core/kernel_main.cpp` 里新增了：

```text
struct UserModeSession
```

你可以把它理解成：

> 汇编入口和 C++ 之间约定的一张“第一次进用户态所需最小清单”。

里面现在先保存：

- 以后恢复内核时要回去的 `kernel_resume_stack_pointer`
- 原来的 `kernel_root_physical`
- 目标 `user_root_physical`
- 用户入口 `RIP`
- 用户栈顶 `RSP`
- 用户 `RFLAGS`
- 用户 `CS`
- 用户 `SS`
- 用户返回值

为什么它要故意写成一排 `uint64_t`？

因为这一轮的重点就是：

```text
让你非常直观看清：
第一次进 ring 3 时，到底要准备哪几个机器级字段
```

另外还配了很多 `static_assert(offsetof(...))`。

原因很简单：

```text
汇编按偏移取字段时，一旦结构布局和 C++ 不一致，就会直接取错值
```

所以这里必须在编译期就把偏移钉死。

---

## 7. 第一版用户程序到底是什么

这一轮没有先做 ELF 加载器。

而是把一小段极短的 ring 3 汇编程序直接嵌在：

```text
kernel/task/context_switch.asm
```

里，名字是：

- `user_mode_smoke_program_start`
- `user_mode_smoke_program_end`

它只做 3 件事：

1. 把 `DS/ES/FS/GS` 设成用户数据段
2. 用 `int 0x80` 执行一次 `write(1, "...")`
3. 再用 `exit` syscall 把当前 `CS` 带回内核

为什么还要自己 `mov ds/es/fs/gs`？

因为：

```text
iretq 只会帮你恢复 SS、RSP、RFLAGS、CS、RIP
不会顺手替你把 DS/ES/FS/GS 也改成用户段
```

所以用户程序一进来先做这一步，
等于把最基本的数据段环境补齐。

---

## 8. `kernel_main` 里这一轮到底怎么跑

这一轮真正的烟测主线在：

```text
run_user_mode_smoke_test(...)
```

它按顺序做这些事：

1. 重新初始化 syscall context，并装好 `sys_write` / 分发器
2. 克隆当前地址空间，得到一份独立的 `user_space`
3. 申请 1 页用户代码页
4. 申请 1 页用户栈页
5. 把嵌入的 ring 3 smoke program 拷进代码页
6. 把代码页映射到 `0x400000`
7. 把栈页映射到 `0x7FF000`
8. 准备用户初始栈顶 `0x800000`
9. 构造 `UserModeSession`
10. 调 `user_mode_enter(&session)`
11. 等用户态通过 `exit` 再接回这里
12. 检查带回来的 `CS == 0x43`，并验证 `CPL == 3`

这里有两个地址你要记一下：

- 用户代码入口：`0x400000`
- 用户栈顶：`0x800000`

第一版故意固定成常量，
不是因为现代 OS 就该永远这么写，
而是因为：

```text
教学第一步先把“用户地址空间里至少有一页代码、一页栈”这件事讲清楚
```

先别引入更多变量。

---

## 9. 为什么这一步先关掉 `IF`

`UserModeSession.user_rflags` 这一轮用的是：

```text
(read_rflags() | 0x2) & ~(1ULL << 9)
```

也就是：

- 保证 bit1 = 1
- 先把 IF 清掉

原因不是“用户态不能开中断”，
而是：

```text
这轮 smoke test 先只验证最核心的 ring 切换闭环，
尽量不要让异步 IRQ 抢进来，把问题复杂度一下抬高
```

等以后用户线程、抢占、返回用户态路径更正式以后，
再仔细决定用户态该带什么 `RFLAGS`。

---

## 10. 为什么还要补一个临时的 `exit` syscall

如果用户程序只会：

```text
write(1, ...)
```

那它最多只能证明：

```text
用户态确实打进过内核
```

但它还不能把控制流稳定地交回：

```text
run_user_mode_smoke_test()
```

所以这一轮额外补了：

```text
kSyscallNumberExit = 10
```

注意这不是完整意义上的进程退出设计，
只是这一轮教学用的最小回程通道。

它的逻辑是：

1. 用户态把一个值作为 `exit` 参数传进来
2. `dispatch_syscall_registers()` 识别到 `kSyscallNumberExit`
3. 内核确认当前确实存在活动中的 `UserModeSession`
4. 调 `kernel_handle_user_mode_exit(return_value)`
5. 再由 `user_mode_resume_kernel(...)` 接回原先的 C++ 调用点

这轮故意把：

```text
用户态看到的 CS
```

作为 `exit` 参数带回来。

原因是这样最容易验证：

```text
我到底是不是真的跑进了 ring 3
```

最后内核打印：

- `user_mode_return_cs=0x0000000000000043`
- `user_mode_return_cpl=3`

这两个值一出来，
就说明这次不是“看起来像用户态”，
而是真的处在 CPL=3。

---

## 11. 这一轮日志为什么值得看

现在正常启动日志里会多出这些关键标记：

- `user_mode_root=0x...`
- `user_mode_code_phys=0x...`
- `user_mode_stack_phys=0x...`
- `user_mode_entry=0x0000000000400000`
- `user_mode_stack_top=0x0000000000800000`
- `user_mode_program_size=...`
- `user_mode_message=hello from ring3 via int80`
- `user_mode_return_cs=0x0000000000000043`
- `user_mode_return_cpl=3`
- `user mode ok`

它们分别证明了：

1. 的确单独准备了一份用户页表根
2. 的确单独准备了代码页和栈页
3. 的确落到了设定的用户虚拟地址
4. 的确从 ring 3 打了一次 `int 0x80`
5. 的确带着用户态的 `CS` 回来了

---

## 12. 这一轮踩到过什么坑

### 12.1 `UserModeSession session{}` 在当前环境下会触发 SSE 指令

一开始如果在 C++ 里写：

```cpp
UserModeSession session{};
```

编译器在当前构建参数下可能会生成 XMM/SSE 清零指令。

但现在内核早期还没有把这套 FPU/SSE 环境正式打开，
结果第一次走到这里就可能直接 `#UD`。

所以这一轮改成了：

```cpp
UserModeSession session;
memory_set(&session, 0, sizeof(session));
```

这样就不会偷偷引入当前阶段还没准备好的指令集路径。

### 12.2 用户页不只要叶子 PTE 有 `U=1`

这个上面已经讲过。

如果父级页表项没有 user 位，
用户态在 `0x400000` 取第一条指令时就会 page fault，
错误码通常会表现成“用户态访问违规”。

这就是为什么 `paging.cpp` 里必须把 user/writable 标志一路传播到父级表项。

---

## 13. 这一轮为什么说“已经第一次真正进入用户态”

因为这次满足了 3 个条件：

### 条件 1：CPU 的 `CS` 真的变成了 ring 3 代码段

最后带回来的：

```text
0x43
```

不是普通数字，
而是：

```text
用户代码段选择子 0x40 + RPL 3
```

### 条件 2：返回的 CPL 真的是 3

`CS & 0x3 == 3`

这说明 CPU 当前权限级确实变了。

### 条件 3：用户代码不是“假装执行”，而是真的用 ring 3 身份触发了 `int 0x80`

日志里那句：

```text
user_mode_message=hello from ring3 via int80
```

就是证据。

所以这一步不是“内核模拟用户态”，
而是 CPU 真的完成了一次：

```text
ring 0 -> ring 3 -> ring 0
```

---

## 14. 这一步还不等于“已经有正式用户进程”

这个区别一定要看清。

现在还没有正式完成的包括：

- ELF 用户程序加载
- 用户进程退出回收
- 用户线程调度切换
- 用户地址空间和内核高半区的更正式分离
- `fork/exec`
- `syscall/sysret`
- 用户页缺页处理

所以这一轮更准确的名字应该是：

```text
第一次真正进入用户态的 smoke test
```

它的价值不是“功能已经完善”，
而是：

> 最关键、最危险、最机器级的那条 ring 切换链路已经第一次打通了。

---

## 15. 下一步最合理做什么

这一步之后，最合理的方向通常不是马上去加更多 shell 命令，
而是把这条教学版用户态路径升级成更正式的进程/线程模型。

最自然的下一批工作大致是：

1. 给用户线程准备真正的 trap frame / 初始上下文对象，而不是只靠一次性 `UserModeSession`
2. 把“用户态 exit 回内核测试函数”升级成正式的线程退出 / 进程退出路径
3. 让 scheduler 真正能调度“内核线程”和“用户线程”
4. 把 `SyscallContext` 逐步挂到真正的进程对象上
5. 再往后才是 ELF 用户程序加载和更正式的地址空间布局

一句话记住这一轮：

```text
TSS 解决“从用户态回来站哪根内核栈”
AddressSpace 解决“用户代码活在哪份页表”
iretq 解决“怎么第一次真的落进 ring 3”
int 0x80 解决“怎么从 ring 3 真的回到 ring 0”
```
