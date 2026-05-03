# 从 syscall 上下文到第一版 `int 0x80` 软中断入口

这份文档讲的是这一轮真正往前推进了什么：

> 不再只是在 C++ 里直接调用 `sys_open()`、`sys_read()`，而是第一次让 CPU 真正执行 `int 0x80`，从 IDT 里的软中断门进入内核，再把结果带回来。

这一步非常重要，
因为它意味着这个项目的 syscall 已经不只是“函数外观”，
而开始变成“真正的内核入口路径”了。

---

## 1. 这一步到底在解决什么

前一轮我们已经有这些东西：

- `SyscallContext`
- `sys_open`
- `sys_read`
- `sys_close`
- `sys_getcwd`
- `sys_chdir`
- `sys_stat_path`
- `sys_listdir`

但它们当时还有一个明显特点：

```text
它们只是 C++ 函数
```

也就是说，
`kernel_main.cpp` 或 shell 想用它们时，
还是直接在内核里：

```text
call sys_open(...)
call sys_read(...)
```

这能证明“接口设计通了”，
但还不能证明：

- IDT 里有没有专门的 syscall 门
- CPU 真的能不能打一趟 syscall 中断
- 汇编 stub 能不能把寄存器安全交给 C++
- 返回值能不能再从 C++ 写回寄存器带出来

所以这一轮真正补的是：

```text
C++ syscall 外观
-> int 0x80 软中断门
-> 汇编保存寄存器
-> C++ 分发器
-> 返回到触发点
```

---

## 2. 为什么先做 `int 0x80`，不是直接上 `syscall/sysret`

因为你现在还是在“小白也能跟上”的阶段，
而 `syscall/sysret` 一上来会把很多新问题一起带进来：

- `STAR/LSTAR/SFMASK` 这些 MSR
- 更严格的 64 位 ABI 差异
- 用户态 / 内核态切换细节
- 内核栈切换
- `swapgs`
- 将来 ring 3 的返回路径

这会一下子把复杂度抬得很高。

所以这一轮先选：

```text
int 0x80
```

原因很现实：

1. 它直接走现成 IDT
2. 你已经有异常/IRQ stub 框架了
3. 它的“进入内核”路径更容易画出来、讲清楚
4. 现在即使还没有完整用户态，也能先在 ring 0 里做烟测

所以它不是“最终最现代的系统调用指令”，
而是：

> 当前最适合把 syscall 真正接进 CPU 入口链的第一步。

---

## 3. 这一轮改了哪些文件

这一轮主要改了：

- `kernel/interrupts/interrupts.hpp`
- `kernel/interrupts/interrupts.cpp`
- `kernel/interrupts/interrupt_stubs.asm`
- `kernel/syscall/syscall.hpp`
- `kernel/syscall/syscall.cpp`
- `kernel/core/kernel_main.cpp`
- `scripts/test-stage1.sh`
- `scripts/test-invalid-opcode.sh`
- `scripts/test-page-fault.sh`

一句话分工：

- `interrupts/*` 负责把 `int 0x80` 接进 IDT
- `syscall/*` 负责把寄存器翻译成 `sys_*` 调用
- `kernel_main.cpp` 负责做第一轮 `int 0x80` 烟测
- `scripts/test-*.sh` 负责把这条新路径也纳入自动回归

---

## 4. 现在第一版寄存器 ABI 是什么

这一步要先约定：

> 触发 syscall 时，哪个寄存器放什么。

这一轮故意先定一个很小、很好记的 ABI：

```text
RAX = syscall 编号
RDI = 第 1 个参数
RSI = 第 2 个参数
RDX = 第 3 个参数
RCX = 第 4 个参数
RAX = 返回值
```

例如：

```text
RAX = kSyscallNumberOpen
RDI = "guide.txt" 的地址
```

进入内核后，
分发器看到 `RAX` 里的编号是 `open`，
就会去调用：

```cpp
sys_open(context, "guide.txt")
```

然后把返回的 fd 再写回 `RAX`。

这样 `int 0x80` 回来以后，
调用点直接从 `RAX` 里拿结果就行。

---

## 5. IDT 里这一步到底加了什么

原来 IDT 里已经有两类门：

1. CPU 异常门
2. PIC IRQ 门

这一轮再补第 3 类：

```text
vector 0x80
```

也就是：

```text
128 号向量
```

为什么很多系统教程爱用 `0x80`？

因为这是一个非常经典的“软件中断做 syscall”编号，
一看就知道它不是硬件 IRQ，
也不是 CPU 保留的前 32 个异常向量。

这一轮还专门把这个门的属性设成：

```text
DPL = 3
```

意思是：

- 现在 ring 0 里当然能触发它
- 以后真的有 ring 3 用户态时，也可以继续沿用这扇门

所以虽然你现在还没做用户态，
这一步已经提前把门权限留对了。

---

## 6. 汇编 stub 这一步到底做了什么

`int 0x80` 发生时，
CPU 只会先自动压入最基础的返回现场，比如：

- `RIP`
- `CS`
- `RFLAGS`

但 syscall 还需要别的寄存器参数：

- `RAX`
- `RDI`
- `RSI`
- `RDX`
- `RCX`

所以汇编 stub 这一步要做的是：

1. 先手工补一个 `error_code = 0`
2. 再补上 `vector = 128`
3. 把通用寄存器全部压栈保存
4. 把整块寄存器帧地址传给 `kernel_handle_syscall`
5. C++ 处理完以后，再按相反顺序恢复寄存器
6. `iretq` 返回到触发点

这里最关键的一点是：

> 返回值不是单独“传回去”的，而是直接写回保存好的 `rax` 槽位。

等 stub 最后 `pop rax` 时，
新的返回值就自然回到了真正的 `RAX` 里。

这就是“从内核把 syscall 结果带回调用点”的核心机制。

---

## 7. 为什么还需要“当前激活的 syscall 上下文”

你可能会问：

> 既然已经能打进内核了，为什么还要 `SyscallContext`？

因为 syscall 不是孤立函数，
它需要知道：

- 当前 fd 表是谁
- 当前 cwd 是什么

这一轮还没有：

- 进程表
- 调度器
- 当前线程

所以最简单的做法是：

```text
先约定一个“当前激活的 syscall 上下文”
```

也就是：

- `install_syscall_dispatch_context(context)`

这样 `int 0x80` 真的打进来时，
分发器先不去查“当前进程”，
而是先看这个全局激活上下文。

这不是最终形态，
但非常适合作为当前过渡阶段：

> 先把 syscall 入口链打通，再把“谁是 current process”补上。

---

## 8. 这一轮自动烟测验证了什么

这一轮在 `kernel_main.cpp` 里新增了一组 `int 0x80` 烟测。

串口里现在会看到这些新增日志：

- `int80_cwd=/`
- `int80_cwd_after_cd=/docs`
- `int80_listdir_count=1`
- `int80_path_stat_inode=5`
- `int80_write_stdout_payload=hello int80_write`
- `int80_write_stderr_payload=error int80_write`
- `int80_open=3`
- `int80_fd_stat_inode=5`
- `int80_write_stdout_bytes=18`
- `int80_write_stderr_bytes=18`
- `int80_write_file_status=-6`
- `int80_write_bad_fd=-4`
- `int80_read_total=193`
- `int80_eof_read=0`
- `int80_open_count=0`
- `int80_bad_result=0xFFFFFFFFFFFFFFFF`
- `int80_syscall ok`

这些日志分别在证明：

- `getcwd` 真的能通过 `int 0x80` 返回 `/`
- `chdir("docs")` 真的能通过寄存器参数改 cwd
- `listdir(".")` 已经会走 cwd + 目录路径解析
- `stat_path("guide.txt")` 已经能通过软中断进入内核
- `write(stdout/stderr)` 已经能通过软中断把字节写到当前控制台输出路径
- `open/read/seek/close` 已经能完整走一遍 soft interrupt 路径
- 对外公开 fd 已经开始预留 `0/1/2 = stdin/stdout/stderr`
- 对普通文件写入现在会明确返回 `-6`
- 未知 syscall 编号会返回 `-1`

这里的：

```text
0xFFFFFFFFFFFFFFFF
```

其实就是：

```text
-1
```

只是当前日志函数打印的是 64 位十六进制补码形式。

---

## 9. 这一步实现了什么，还没实现什么

### 已经实现的

这一步现在已经实现了：

1. 第一版真正的 syscall 中断门
2. 第一版寄存器 ABI
3. 汇编 stub 保存/恢复寄存器
4. C++ 分发器把编号转到 `sys_*`
5. 返回值从内核写回 `RAX`

### 还没实现的

这一步还没有实现：

1. ring 3 用户态程序
2. 用户态 / 内核态栈切换
3. 用户指针合法性检查
4. `syscall/sysret` MSR 路径
5. 当前进程 / 当前线程的正式管理

所以要很清楚：

> 这一轮做的是“真正 syscall 入口的第一版骨架”，不是完整用户态系统。

---

## 10. 下一步最合理做什么

现在最稳的下一步一般有两条：

第一条是继续补 syscall 服务面：

```text
目录 fd / readdir
更正式的错误码体系
stdin/read
```

第二条是开始准备最小用户态：

```text
ring 3
TSS
内核栈切换
用户程序入口
```

这一步后来已经继续推进到了：

```text
公开 syscall fd 0/1/2 + 第一版 sys_write
```

所以对当前项目来说，
更稳的下一步通常还是：

```text
先把 syscall / I/O 边界再补一点
再做最小用户态
```

原因很简单：

> 先把“用户到底要向内核要什么”和“这些输入输出从哪里进出”定义清楚，再去做 ring 3，调试成本更低。

继续阅读：

```text
docs/KERNEL_SYSCALL_WRITE_GUIDE.md
```
