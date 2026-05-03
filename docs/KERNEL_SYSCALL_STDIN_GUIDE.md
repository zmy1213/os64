# 从公开 fd + `sys_write` 到第一版 `stdin/read(0)`

这一步继续沿着 syscall 的输入输出边界往前走。

上一轮我们已经有：

```text
0 = stdin
1 = stdout
2 = stderr
3+ = 普通文件
```

而且已经实现了：

```text
sys_write(stdout/stderr)
```

但还缺另一半：

```text
sys_read(stdin)
```

所以这一轮真正补的是：

> 让 syscall 这一层第一次能从键盘字符缓冲里读输入。

---

## 1. 这一步到底解决什么问题

前面 shell 和 console 已经能读键盘，
但那条链路是：

```text
keyboard IRQ
-> KeyboardInputEvent
-> console_read_line_with_history
-> shell
```

也就是说：

```text
这是“交互式行输入”
```

它适合 shell，
但还不是更底层、更通用的：

```text
read(0, buffer, size)
```

如果后面你想做最小用户程序，
那它最自然想要的接口不会是：

```text
console_read_line_with_history(...)
```

而是：

```text
read(0, ...)
write(1, ...)
```

所以这一步的意义是：

```text
把“键盘输入”第一次整理成标准输入 stdin 的 syscall 形状
```

---

## 2. 为什么先做 stdin，不先做 ring 3

原因和前一步做 `sys_write` 一样：

```text
先把服务边界补完整
再去做权限边界
```

如果现在直接上用户态，
你会同时面对：

- ring 3 / ring 0 切换
- 用户程序入口
- 用户栈
- 内核栈切换
- 指针检查
- stdin/stdout 到底怎么接

而当前更稳的顺序是：

```text
先把 read(0) / write(1,2) 这种基础 I/O 形状理顺
再让用户程序真正进来
```

---

## 3. 第一版 stdin 到底从哪里读

当前第一版不是从“终端设备文件”读，
因为项目里还没有完整的设备模型。

它现在的路径是：

```text
sys_read(fd=0)
-> keyboard 输入队列
-> 已翻译好的字符事件
```

也就是：

```text
扫描码
-> 键盘中断里翻译成字符/方向键事件
-> stdin 只取字符型事件
```

这一点很关键：

```text
stdin 现在读的是“已经翻译好的字符流”
不是原始扫描码流
```

所以用户看到的是：

- `'a'`
- `'1'`
- `' '`
- `'\n'`
- `'\b'`

而不是：

- `0x1E`
- `0x02`
- `0x39`

---

## 4. 为什么方向键会被 stdin 忽略

现在键盘层已经不只有字符，
还有这些输入事件：

- `ArrowUp`
- `ArrowDown`
- `ArrowLeft`
- `ArrowRight`
- `Delete`
- `Home`
- `End`

这些东西对 shell 的行编辑很有用，
但对第一版字节流 stdin 来说，
它们不是普通字符。

所以这一轮故意做了一个区分：

### 给 console / shell 的接口

```text
keyboard_try_read_input_event()
```

它会保留方向键等编辑事件。

### 给 stdin 的接口

```text
keyboard_try_read_stream_char()
```

它会：

```text
跳过非字符事件
只返回字符型输入
```

所以你可以把这一步理解成：

> 同一个底层键盘队列，开始分化出“事件视角”和“字节流视角”两种消费方式。

---

## 5. `read(0)` 现在是阻塞的还是非阻塞的

最开始这一层只是一个“半阻塞”版本，
后来在补完第一版 scheduler 的 `blocked/wakeup` 骨架以后，
`stdin` 又继续往前升级了一小步：

```text
如果当前已经跑在线程上下文里，
read(0) 没字符时会真的把当前线程挂进 keyboard wait queue
等字符 IRQ 到来再唤醒
```

规则是：

1. 如果缓冲区里已经有字符，就立刻尽量多读
2. 如果一个字符都没有，而且当前正在线程上下文里，就 block 当前线程
3. 键盘 IRQ 到来并收到字符后，再把这个线程放回 ready queue
4. 如果一个字符都没有，而且中断关着，就先返回 `0`

为什么不一上来就做严格 POSIX 语义？

因为当前项目还没有：

- 信号
- 非阻塞标志
- TTY 行规程
- EOF 语义
- 用户态进程模型

所以这里先求：

```text
行为稳定、好讲清楚、不把内核卡死
```

这比假装自己已经有完整 Unix 终端语义更诚实。

---

## 6. 为什么要检查“键盘是否已经初始化”

如果 `sys_read(0, ...)` 在键盘子系统还没准备好时就开始等，
最坏情况会变成：

```text
永远等不到任何字符
```

所以这一轮专门补了：

```text
keyboard_is_ready()
```

这意味着：

- 键盘没初始化：返回 `kSyscallUnsupported`
- 键盘已经初始化：才能真的尝试读 stdin

这一步的本质是在补：

```text
“这个 syscall 现在有没有前置条件”
```

---

## 7. 这一轮改了哪些文件

这一轮主要改了：

- `kernel/interrupts/keyboard.hpp`
- `kernel/interrupts/keyboard.cpp`
- `kernel/syscall/syscall.cpp`
- `kernel/task/scheduler.hpp`
- `kernel/task/scheduler.cpp`
- `kernel/core/kernel_main.cpp`
- `scripts/test-stage1.sh`
- `scripts/test-invalid-opcode.sh`
- `scripts/test-page-fault.sh`
- `README.md`
- `docs/README.md`
- `kernel/README.md`

一句话分工：

- `keyboard/*` 负责给 stdin 提供“字节流视角 + keyboard wait queue”
- `syscall.cpp` 负责把 `fd=0` 真正接进 stdin，并在没字符时选择“直接 block 当前线程”
- `scheduler.*` 负责提供“安全登记等待者后再 block，自身睡下去前重新开中断”的那条底层能力
- `kernel_main.cpp` 负责新增自动烟测

---

## 8. 启动日志现在会多出什么

这一轮新增的关键日志有：

```text
stdin_block_pid=4
stdin_block_reader_tid=9
stdin_block_injector_tid=10
stdin_block_read=1
stdin_block_char=0x0000000000000061
stdin_block_buffer_remaining=0
stdin_sys_read=3
stdin_sys_empty=0
stdin_int80_read=3
stdin_int80_empty=0
stdin_buffer_remaining=0
stdin_dropped_chars=0
stdin_syscall ok
```

这些日志分别在证明：

- `stdin_reader` 线程会先真的睡进 keyboard wait queue
- `stdin_injector` 线程注入一个 `'a'` 扫描码以后，reader 会被键盘 IRQ 唤醒
- reader 被唤醒后，`sys_read(0, ...)` 会继续返回 1 个字节，而且缓冲区没有残留
- 直接 `sys_read(0, ...)` 已经能读到 3 个字符
- 缓冲区清空后，再读一次当前会返回 `0`
- `int 0x80` 入口也已经能走 `read(0, ...)`
- 读完以后键盘字符缓冲区没有残留
- 这一轮测试没有因为缓冲区太小而丢字符

其中测试里还专门混进了方向键扫描码，
就是为了验证：

```text
stdin 字节流不会被 ArrowUp / ArrowLeft 卡住
```

---

## 9. 现在 syscall I/O 边界长什么样

你现在可以把这一层想成：

```text
read(0, ...)  -> 键盘字符流
write(1, ...) -> stdout
write(2, ...) -> stderr
open(...)     -> 3 号以后普通文件
read(3+, ...) -> 只读 OS64FS 文件
write(3+, ...) -> 现在明确不支持
```

这已经很像一个真正操作系统最早期的用户态 I/O 骨架了。

虽然它还很小，
但边界开始成形了。

---

## 10. 这一轮你真正学到了什么

这一步真正重要的不是“多了个 read(0)”。

而是：

```text
同一份输入数据
对不同上层来说
可以有不同的抽象视角
```

比如：

- console/shell 想看“方向键、Home、End、Delete”这种编辑事件
- stdin 想看“字符字节流”

所以内核不应该把所有消费者都绑死在同一种输入表示上。

这就是为什么我们现在开始区分：

```text
KeyboardInputEvent
vs
stdin byte stream
```

---

## 11. 下一步最合理做什么

这一步做完以后，当前项目后来是继续往前补了：

```text
第一版 process/thread/scheduler 骨架
```

也就是：

```text
先让系统里第一次真正出现“可调度线程”
再把 stdin 的等待关系真的挂进 scheduler
```

原因是：

> 你已经有了输入输出边界，但还没有“谁在系统里轮流执行”这层对象；先把 thread/scheduler 骨架搭起来，`stdin` 的真正阻塞才能不靠轮询，而是靠 thread block/wakeup 做成更像真实 OS 的形状。

继续阅读：

```text
docs/KERNEL_TASKING_GUIDE.md
```
