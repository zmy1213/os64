# 从文件描述符表到 shell 当前工作目录

上一轮我们做了：

```text
fd_open
fd_read
fd_close
```

也就是让 `cat` 更像真实系统里的：

```text
open(path)
read(fd)
close(fd)
```

这一轮继续补一个 shell 里非常基础、但很重要的概念：

```text
cwd
```

cwd 的全称是：

```text
current working directory
当前工作目录
```

简单理解：

> 你现在“站”在哪个目录里。

---

## 1. 为什么要做 cwd

没有 cwd 时，
你每次访问子目录里的文件都要写完整路径：

```text
cat /docs/guide.txt
```

有了 cwd 后，
你可以先进入目录：

```text
cd docs
```

然后直接写：

```text
cat guide.txt
```

这更像真实 shell。

更重要的是，
它让你开始理解真实 OS 里这条链：

```text
用户输入的相对路径
-> 当前工作目录
-> 规范化后的绝对路径
-> VFS
-> 文件系统
```

---

## 2. 这一轮新增了什么能力

新增命令：

```text
pwd
cd [path]
```

并且这些命令现在会按 cwd 解析相对路径：

```text
ls
cat
stat
```

也就是说：

```text
cd docs
ls
cat guide.txt
```

现在可以工作。

---

## 3. cwd 放在哪里

这次没有把 cwd 放进 `VFS`，
也没有放进 `OS64FS`。

而是放在：

```text
ShellState
```

代码位置：

```text
kernel/shell/shell.hpp
```

里面新增了：

```cpp
char current_working_directory[kShellPathCapacity];
```

为什么放这里？

因为 cwd 是“当前 shell 会话”的状态。

`OS64FS` 只应该负责：

```text
给我一个路径，我帮你找 inode
```

`VFS` 只应该负责：

```text
给上层统一入口
```

而“用户现在站在哪个目录里”，
是 shell 自己的交互状态。

所以当前分层是：

```text
shell 负责 cwd
VFS 负责统一文件系统入口
OS64FS 负责底层格式
```

这样分层更清楚。

---

## 4. 为什么 cwd 保存成绝对路径

当前 cwd 始终保存成：

```text
/
/docs
```

这种绝对路径。

不会保存成：

```text
docs
./docs
docs/..
```

原因是绝对路径更稳定。

比如你现在在：

```text
/docs
```

输入：

```text
cat guide.txt
```

shell 会先把它变成：

```text
/docs/guide.txt
```

再交给 VFS。

这样 VFS 和文件系统不用猜：

```text
guide.txt 到底是根目录下的 guide.txt，
还是 /docs 下面的 guide.txt？
```

---

## 5. 路径规范化做了什么

这一轮 shell 里新增了路径规范化逻辑。

它会处理：

```text
.
..
/
多个连续 /
相对路径
绝对路径
```

例子：

```text
cwd = /
docs/guide.txt
-> /docs/guide.txt
```

```text
cwd = /docs
guide.txt
-> /docs/guide.txt
```

```text
cwd = /docs
../notes.txt
-> /notes.txt
```

```text
cwd = /docs
.
-> /docs
```

注意：

> 这一轮只是规范化路径字符串，不是实现真正的进程 cwd。

因为当前内核还没有进程。

以后有进程后，
每个进程应该有自己的 cwd。

现在先放在全局 shell 状态里。

---

## 6. pwd 做了什么

`pwd` 很简单。

它只打印：

```text
pwd_path=<当前 cwd>
```

启动自测里会看到：

```text
pwd_path=/
```

执行：

```text
cd docs
pwd
```

以后会看到：

```text
pwd_path=/docs
```

---

## 7. cd 做了什么

`cd docs` 的流程是：

```text
1. 把 docs 按当前 cwd 解析成 /docs
2. 调 vfs_stat("/docs")
3. 检查 /docs 必须是目录
4. 把 ShellState.current_working_directory 改成 /docs
```

为什么要先 `vfs_stat`？

因为不能随便把 cwd 改成一个不存在的路径。

也不能把 cwd 改成普通文件：

```text
cd readme.txt
```

这种应该失败。

---

## 8. ls/cat/stat 的变化

以前：

```text
cat guide.txt
```

总是从根目录开始找，
所以找不到。

现在：

```text
cd docs
cat guide.txt
```

shell 会先解析成：

```text
/docs/guide.txt
```

再走：

```text
fd_open
fd_read
fd_close
```

为了调试更清楚，
现在文件命令会多打印一行解析后的路径：

```text
cat_path=guide.txt
cat_resolved_path=/docs/guide.txt
```

`ls` 和 `stat` 也类似：

```text
ls_resolved_path=/docs
stat_resolved_path=/notes.txt
```

---

## 9. 顺手修了一个启动卷地址问题

这一轮代码变多后，
内核的 `.bss` 段也变大了。

原来 stage2 把 boot volume 读到：

```text
0x00020000
```

但内核的 `.bss` 增长后会覆盖到这附近。

结果就是：

```text
stage2 明明读了 boot volume
kernel 看到的却是全 0
```

所以这次把 boot volume 预读地址改到：

```text
0x00080000
```

为什么这个位置可以？

- 它仍然在低 2 MiB 里，当前 early paging 恒等映射能访问
- 它离内核镜像和 `.bss` 更远，不容易被清零覆盖
- 它还低于传统 VGA/设备洞附近的危险区域

这说明一个真实内核问题：

> bootloader 搬运数据时，必须避开内核自己的代码段、数据段和 bss 段。

---

## 10. 这一轮怎么测试

构建：

```bash
make stage1
```

正常启动回归：

```bash
make test-stage1
```

异常回归：

```bash
make test-invalid-opcode
make test-page-fault
```

启动日志里现在会看到：

```text
pwd_path=/
cwd_path=/docs
pwd_path=/docs
ls_path=/docs
cat_path=guide.txt
cat_resolved_path=/docs/guide.txt
```

这证明：

```text
cd docs
cat guide.txt
```

已经真的通过 cwd 找到了：

```text
/docs/guide.txt
```

---

## 11. 这一轮你真正学到了什么

这一步学的不是“多两个命令”。

真正学的是：

```text
相对路径不是文件系统自己凭空知道的
```

它需要一个上下文：

```text
cwd
```

现在路径链路变成：

```text
shell input
-> cwd path resolver
-> absolute path
-> fd / VFS
-> OS64FS
-> BlockDevice
-> BootVolume
```

这比上一轮更接近真实 OS。

---

## 12. 下一步最合理做什么

下一步已经继续做：

> 第一版系统调用形状。

也就是先不急着做用户态进程，
而是在内核里把接口设计成更像：

```text
sys_open
sys_read
sys_close
```

原因是：

- fd 表已经有了
- cwd 路径解析已经有了
- VFS 已经有了

接下来就可以开始为“用户程序怎么请求内核服务”铺路。

继续阅读：

```text
docs/KERNEL_SYSCALL_SHAPE_GUIDE.md
```
