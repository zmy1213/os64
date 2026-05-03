# 从第一版 `int 0x80` 到公开 fd + `sys_write`

这一步继续沿着 syscall 这条线往前走，
但目标不是一下子做用户态，
而是先把一个非常关键的能力补上：

```text
让“用户看到的系统调用接口”第一次能把数据写出去
```

也就是：

```text
sys_write
```

不过这一步真正有意思的地方，
不只是“多了一个 write”，
而是顺手把这两个概念拆开了：

```text
内核内部文件表 fd
vs
公开给 syscall 的 fd
```

这一步做完以后：

- `fd_open()` 这一层仍然保持第一版教学形态，第一次打开文件还是返回 `0`
- `sys_open()` / `int 0x80 open` 这一层开始更像真实 OS，第一次打开普通文件会返回 `3`
- `sys_write()` 现在先支持往 `stdout/stderr` 写
- 对普通文件描述符写入会明确返回“当前不支持”

---

## 1. 为什么这一步不是先做 ring 3

因为如果你现在直接跳去做用户态，
会一下子把很多问题都卷进来：

- ring 3 / ring 0 权限切换
- TSS
- 内核栈切换
- 用户指针检查
- 用户程序装载
- 返回路径细节

而 `sys_write` 这一步更像是在先补：

```text
“用户到底能向内核要什么服务”
```

所以顺序更稳的是：

```text
先把 syscall 服务面再补一点
再去做真正用户态
```

这也是现代内核设计里非常常见的思路：

> 先稳定服务边界，再扩权限边界。

---

## 2. 这一步到底在解决什么问题

上一轮我们已经有：

- `sys_open`
- `sys_read`
- `sys_close`
- `sys_seek`
- `sys_stat`
- `sys_getcwd`
- `sys_chdir`
- `sys_stat_path`
- `sys_listdir`
- 第一版 `int 0x80`

但这些接口有一个很明显的共同点：

```text
几乎都在“把东西读进来”
```

你可以打开文件，
读文件，
列目录，
查路径，
改 cwd。

但是你还不能通过 syscall 这层做一件最经典的事：

```text
把一段字节写到标准输出
```

而这件事后面会成为很多功能的共同底座：

- 用户程序 `printf`
- shell 重定向
- 错误输出
- 日志设备
- TTY / 终端

所以这一轮补的是：

```text
第一版公开输出能力
```

---

## 3. 为什么 `sys_open` 现在不再返回 0

这一步最容易让小白困惑，
也是最值得专门讲清楚的一点。

你现在会看到两个看起来矛盾的现象：

```text
fd_open=0
sys_open=3
int80_open=3
```

它们并不矛盾，
因为这三个数字本来就不在同一层。

### `fd_open=0` 代表什么

这是：

```text
内核内部文件描述符表
```

也就是 `kernel/fs/fd.cpp` 那层。

它的职责是：

```text
给“普通打开文件”分配一个内部槽位
```

所以第一版里，
第一个普通文件槽位当然还是 `0`。

### `sys_open=3` 代表什么

这是：

```text
公开给 syscall 使用者看的 fd 编号
```

这一层我们开始预留：

```text
0 = stdin
1 = stdout
2 = stderr
3+ = 普通文件
```

所以现在第一次 `sys_open("guide.txt")`，
虽然内部 `fd_open()` 仍然拿到的是 `0`，
但对外会映射成：

```text
3
```

也就是：

```text
内部文件 fd 0
-> 公开 syscall fd 3
```

---

## 4. 为什么不直接把 `fd` 层也改成从 3 开始

因为这两个层次承担的教学任务不同。

`fd` 层现在的目标是先教你理解：

```text
一个小整数如何对应到一个打开文件对象
```

所以它越朴素越好：

```text
0 号槽位
1 号槽位
2 号槽位
```

这很适合讲“打开文件表”的本质。

而 syscall 层承担的是另一个目标：

```text
逐步长成更像真实操作系统的公开接口
```

所以这里更合理的做法不是推翻前面的 fd 教学层，
而是：

```text
保留内部 fd 0 开始
在 syscall 边界上映射成 0/1/2 预留 + 3 起普通文件
```

这样好处有两个：

1. 前面已经讲清楚的 `fd` 层不需要重写
2. 后面的用户态接口已经开始向真实 OS 靠拢

---

## 5. 这一轮 `sys_write` 到底支持什么

第一版故意只支持：

```text
stdout
stderr
```

也就是：

```text
fd = 1
fd = 2
```

为什么只做这两个？

因为当前文件系统还是：

```text
只读 OS64FS
```

如果你现在假装“普通文件也能 write”，
那反而是在骗自己。

所以更诚实的做法是：

- 对 `stdout/stderr`：真的支持写
- 对普通文件：明确返回 `kSyscallUnsupported`

这比“假装成功”或者“偷偷丢数据”都要好得多。

---

## 6. `stdout/stderr` 现在是怎么写出去的

当前项目还没有真正的：

- 终端设备驱动
- TTY 子系统
- 设备文件
- 用户态进程控制台绑定

所以这一步先不硬做一个假的“设备树”。

现在采取的是一个很小、很干净的过渡办法：

```text
SyscallContext 里放一个 write 回调
```

也就是：

```text
stdout/stderr
-> sys_write
-> SyscallContext.write_handler
-> 当前控制台输出函数
-> VGA + 串口
```

这条路的好处是：

1. syscall 层知道“要写输出”
2. syscall 层不用直接依赖具体 VGA/串口实现细节
3. 以后真有 TTY/设备层时，可以把回调换成正式对象

所以这不是最终终端架构，
而是一个非常合适的过渡骨架。

---

## 7. 为什么 `stdin` 先不支持 `read`

现在公开 fd 已经预留了：

```text
0 = stdin
```

但这不代表这一轮已经把：

```text
read(0, ...)
```

真正做完了。

原因是控制台现在读输入的方式还是：

```text
console_read_line_with_history(...)
```

它是“整行交互”模型，
还不是“任意进程随时从 stdin 读字节流”的模型。

所以这一步先只把编号预留好，
而不装作 `stdin` 已经完整实现。

这也是为什么当前对保留的标准 fd 做不支持操作时，
会返回：

```text
kSyscallUnsupported
```

---

## 8. 这一轮改了哪些文件

这一步主要改了：

- `kernel/syscall/syscall.hpp`
- `kernel/syscall/syscall.cpp`
- `kernel/core/kernel_main.cpp`
- `scripts/test-stage1.sh`
- `README.md`
- `docs/README.md`
- `kernel/README.md`
- `docs/KERNEL_SYSCALL_SHAPE_GUIDE.md`
- `docs/KERNEL_SYSCALL_CWD_GUIDE.md`
- `docs/KERNEL_INT80_SYSCALL_GUIDE.md`

一句话分工：

- `syscall/*` 负责公开 fd 约定和 `sys_write`
- `kernel_main.cpp` 负责做直接调用和 `int 0x80` 烟测
- `test-stage1.sh` 负责把这条新路径纳入自动回归
- 文档负责把“为什么 open 变成 3”讲清楚

---

## 9. 启动烟测现在会看到什么

这一轮新增的重要日志有：

```text
sys_write_stdout_payload=hello sys_write
sys_write_stderr_payload=error sys_write
sys_open=3
sys_write_stdout_bytes=16
sys_write_stderr_bytes=16
sys_write_file_status=-6
sys_write_bad_fd=-4
```

以及：

```text
int80_write_stdout_payload=hello int80_write
int80_write_stderr_payload=error int80_write
int80_open=3
int80_write_stdout_bytes=18
int80_write_stderr_bytes=18
int80_write_file_status=-6
int80_write_bad_fd=-4
```

这些日志分别在证明：

- 直接 `sys_write` 已经能往 `stdout/stderr` 写字节
- `int 0x80` 入口也已经能把 write 参数带进内核
- 对外公开 fd 已经开始保留 `0/1/2`
- 普通文件写入现在会明确返回“不支持”
- 非法 fd 会明确返回 `-4`

这里的：

```text
-6
```

就是：

```text
kSyscallUnsupported
```

意思不是“代码炸了”，
而是：

```text
这个接口形状已经有了，但当前内核版本还没支持这类对象/操作
```

---

## 10. 现在的公开 fd 形状长什么样

你现在可以把 syscall 这一层的 fd 想成：

```text
0 = stdin
1 = stdout
2 = stderr
3 = 第一个普通文件
4 = 第二个普通文件
...
```

而内核内部文件表还是：

```text
0 = 第一个普通文件槽位
1 = 第二个普通文件槽位
2 = 第三个普通文件槽位
...
```

所以 syscall 层现在多做了一次“边界翻译”：

```text
公开 fd
-> 映射成内部 fd
-> 进入 fd 表 / VFS / 文件系统
```

这就是为什么这一步虽然只是补 `sys_write`，
却顺手让整个 syscall 边界更像真实 OS 了。

---

## 11. 这一轮你真正学到了什么

这一轮真正学到的不是：

```text
write 就是多写个函数
```

而是：

```text
内核内部表示
和
对外公开 ABI
不一定要长得一样
```

这是非常关键的系统设计观念。

因为真实内核里经常都是这样：

- 内部对象怎么放，是内核自己的事
- 对外 ABI 怎么稳定，是给用户程序的承诺

这两层最好解耦。

---

## 12. 下一步最合理做什么

现在最顺的下一步一般有两条：

这一步后来继续推进到了：

```text
第一版 stdin/read(0)
```

也就是：

- `sys_read(0, ...)` 直接从键盘字符流读输入
- `int 0x80` 版本也能走同一条 stdin 路径
- 方向键这类非字符事件暂时留给 console/shell，不进入字节流 stdin

所以接下来最顺的方向仍然是：

```text
继续补 syscall / I/O 边界
```

比如：

```text
目录 fd / readdir
更正式的终端/TTY 抽象
```

第二条是开始最小用户态：

```text
ring 3
TSS
内核栈切换
用户程序入口
```

对当前项目来说，
更稳的顺序通常还是：

```text
先把 syscall / I/O 边界再补一点
再做最小用户态
```

原因是：

> 用户态一旦进来，调试难度会突然上升；先把边界继续做实，后面会轻松很多。

继续阅读：

```text
docs/KERNEL_SYSCALL_STDIN_GUIDE.md
```
