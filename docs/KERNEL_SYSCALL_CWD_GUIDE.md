# 从第一版系统调用形状到 syscall 上下文里的 cwd

上一轮我们已经有了：

```text
sys_open
sys_read
sys_stat(fd)
sys_seek
sys_close
```

但那一轮的 `SyscallContext` 还很瘦：

```cpp
struct SyscallContext {
  FileDescriptorTable* fd_table;
};
```

这说明它只知道：

```text
打开文件要走哪张 fd 表
```

还不知道：

```text
当前工作目录 cwd 是什么
```

所以虽然 shell 已经支持：

```text
pwd
cd
ls
cat guide.txt
```

但那时的 cwd 其实还只是 shell 自己私下保存的一份状态。

这一轮补的是：

```text
把 cwd 从 shell 私有状态抬进 SyscallContext
```

顺手再补三类 syscall 风格接口：

```text
sys_getcwd
sys_chdir
sys_stat_path
sys_listdir
```

你可以把这一轮理解成：

> SyscallContext 开始真的像“进程状态”了，而不只是一个 fd 表指针。

---

## 1. 为什么 cwd 不该长期留在 shell 里

如果 cwd 只存在于 shell 里，
链路是：

```text
shell
-> 自己解析相对路径
-> VFS / fd
```

这样能跑，
但问题很明显：

```text
只有 shell 知道当前目录
```

以后真正有用户程序时，
你会遇到一个根问题：

```text
不是所有进程都是 shell
```

比如以后你做一个最小用户程序：

```c
open("guide.txt");
```

那这个相对路径到底相对谁？

答案应该是：

```text
相对当前进程的 cwd
```

所以 cwd 更像：

```text
进程上下文的一部分
```

而不是 shell UI 的私有变量。

---

## 2. 这一轮真正改了什么结构

现在 `SyscallContext` 变成：

```cpp
struct SyscallContext {
  FileDescriptorTable* fd_table;
  char current_working_directory[64];
};
```

这多出来的不是“一个普通字符串”。

它代表的是：

```text
这组系统调用当前站在哪个目录里看文件系统
```

小白版理解：

```text
以前 shell 自己记着“我现在在 /docs”
现在 syscall 上下文自己记着“我现在在 /docs”
shell 只是来问它，不再自己私藏一份
```

---

## 3. 为什么这更接近真实操作系统

真实系统里，
用户程序做这些事时：

```c
chdir("docs");
open("guide.txt");
```

第二句之所以能找到：

```text
/docs/guide.txt
```

不是因为 shell 帮它预先改了字符串，
而是因为：

```text
内核知道这个进程当前 cwd 是 /docs
```

然后内核自己解析：

```text
guide.txt
-> /docs/guide.txt
```

所以这一步真正往前推进的是：

```text
相对路径解析的“归属权”
```

它从 shell 手里往内核 syscall 上下文里移动了。

---

## 4. 这轮新增了哪些接口

现在 syscall 方向多了这些能力：

```text
sys_getcwd
sys_chdir
sys_stat_path
sys_listdir
```

它们分别在解决不同问题。

### `sys_getcwd`

作用：

```text
把当前 cwd 拿出来
```

现在 `pwd` 命令不再读 shell 里的 `current_working_directory`，
而是去问 syscall 上下文：

```text
你当前 cwd 是什么？
```

### `sys_chdir`

作用：

```text
把 cwd 改成另一个目录
```

它内部会做三件事：

```text
1. 解析相对路径
2. 确认路径存在
3. 确认目标是目录
```

如果都成功，
才会真正改写：

```text
context->current_working_directory
```

### `sys_stat_path`

上一轮的 `sys_stat` 是：

```text
stat 一个已经打开的 fd
```

这一轮新增的是：

```text
stat 一个路径
```

所以现在有两种 stat：

```text
sys_stat(fd, ...)
sys_stat_path(path, ...)
```

这跟真实系统里“基于路径”和“基于打开文件对象”的两类入口很像。

### `sys_listdir`

作用：

```text
按路径列目录
```

第一版先做成最简单的形状：

```text
给一个路径
返回目录项数量
或者把目录项拷贝到一块固定数组里
```

它还没有做成“打开目录 -> readdir -> close”这种更正式的目录 fd 模型，
但已经足够让 shell 的 `ls` 走进 syscall 层。

---

## 5. 这一轮 shell 到底怎么变了

这一轮最关键的变化不是“多了几个命令”。

而是这些老命令开始改道：

```text
pwd
cd
ls
cat
stat
```

以前它们大致是：

```text
shell
-> shell 自己的 cwd
-> VFS / fd
```

现在它们更像：

```text
shell
-> SyscallContext
-> sys_*
-> fd / VFS
-> OS64FS
```

这说明 shell 正在从“直接操纵文件系统层的前端”
往“系统调用调用者”这个角色靠近。

---

## 6. 为什么还保留 resolved path 日志

启动日志里你现在仍然能看到：

```text
cat_path=guide.txt
cat_resolved_path=/docs/guide.txt
```

这不是多余。

它的教学价值很高，
因为它能让你同时看见两件事：

```text
用户输入了什么
内核最终把它解释成了什么绝对路径
```

否则你只看 `cat` 成功，
很难确定到底是：

- cwd 真起作用了
- 还是某层恰好偷偷用了绝对路径

---

## 7. syscall 烟测新增验证了什么

`kernel_main.cpp` 里的 `run_syscall_smoke_test` 现在不只测：

```text
open / read / seek / close
```

它现在还会测：

```text
sys_cwd=/
sys_root_entries=3
sys_cwd_after_cd=/docs
sys_listdir_count=1
sys_path_stat_inode=5
sys_open=3
```

这些日志分别在证明：

- syscall 上下文初始化后 cwd 真的是 `/`
- 根目录确实能通过 syscall 列出 3 个目录项
- `sys_chdir("docs")` 后 cwd 真的变成 `/docs`
- 在 `/docs` 里列目录只会看到 1 个 `guide.txt`
- `sys_stat_path("guide.txt")` 已经能按相对路径找到 inode 5
- `sys_open("guide.txt")` 也已经能按相对路径打开文件

这里现在之所以是：

```text
sys_open=3
```

不是因为 fd 层变了，
而是因为后来 syscall 对外又预留了：

```text
0 = stdin
1 = stdout
2 = stderr
3+ = 普通文件
```

也就是说：

```text
内部 fd 表里的第一个普通文件槽位还是 0
但 syscall 边界对外会把它映射成 3
```

这说明：

```text
cwd -> 相对路径解析 -> stat/open/listdir
```

这一整条链已经真的进入 syscall 层。

---

## 8. 为什么这一步会把 kernel.bin 撑大

这一轮除了 syscall 新接口，
还顺手暴露出一个现实问题：

```text
kernel.bin 变大到了 128 个扇区
```

而构建脚本里原来还保留着旧限制：

```text
最多 127 个扇区
```

这在更早的时候是合理的，
因为那时大家默认 BIOS 一次大块读盘会受 `127` 之类的限制影响。

但现在 stage2 的读盘逻辑已经是：

```text
每轮只读 1 个扇区
循环把整段数据读完
```

所以真正合理的限制不再是：

```text
内核不能超过 127 扇区
```

而是：

```text
内核 + boot volume 不能把整张软盘镜像塞爆
```

这也是为什么构建脚本这一轮一起改了。

不过这里后来又继续暴露出一个比“127 扇区限制”更隐蔽的真实 loader 坑：

```text
128 个扇区 = 64 KiB
```

stage2 读盘时用的是 BIOS 传统的：

```text
ES:BX
```

也就是实模式的 `segment:offset` 目标地址。

如果每轮只是简单地把：

```text
offset += 512
```

那在前 `128` 个扇区都读完以后，
16 位 offset 会正好从 `0xFE00 + 0x0200` 回卷到 `0x0000`。

这时如果 kernel 恰好长到第 `129` 个扇区，
而 segment 没有同时前进，
第 `129` 个扇区就会直接覆盖第 `1` 个扇区。

所以后来 stage2 又补了一步：

- offset 回卷时
- segment 额外加 `0x1000`

原因是：

```text
linear = segment * 16 + offset
```

offset 多跨过去的那 `0x10000` 字节，
要靠 `segment += 0x1000` 才能在线性地址上继续接上。

---

## 9. 现在这条链长什么样

以：

```text
cd docs
cat guide.txt
```

为例，
现在更像：

```text
shell
-> sys_chdir("docs")
-> SyscallContext.cwd = /docs

shell
-> sys_open("guide.txt")
-> 相对路径解析成 /docs/guide.txt
-> fd_open
-> VFS
-> OS64FS

shell
-> sys_read(fd)
-> fd_read
-> VFS
-> OS64FS
-> BlockDevice
-> BootVolume
```

这已经比“shell 自己改字符串再直接碰 VFS”更接近真实内核结构。

---

## 10. 这一轮你真正学到了什么

这一轮真正学到的不是：

```text
sys_getcwd / sys_chdir / sys_listdir 这几个名字
```

真正学到的是：

```text
系统调用上下文里应该装什么状态
```

目前已经有两类很关键的东西进去了：

- fd 表
- cwd

以后还会继续往里长：

- 当前进程
- 地址空间
- 权限信息
- 用户栈 / 内核栈切换相关状态

所以 `SyscallContext` 现在虽然还小，
但它已经开始有“进程内核态画像”的味道了。

---

## 11. 这一步后来继续推进到了哪里

这一步后来没有直接跳去完整用户态，
而是先补上了：

```text
第一版 int 0x80 软中断 syscall 入口
```

也就是：

- CPU 真正执行一次 `int 0x80`
- IDT 里的 0x80 号门接住它
- 汇编 stub 保存寄存器
- C++ 分发器再转到现有 `sys_*`

这一轮后来又继续往前补了：

```text
第一版 int 0x80 软中断 syscall 入口
公开 syscall fd 0/1/2 + 第一版 sys_write
```

对应继续阅读：

```text
docs/KERNEL_INT80_SYSCALL_GUIDE.md
docs/KERNEL_SYSCALL_WRITE_GUIDE.md
```
