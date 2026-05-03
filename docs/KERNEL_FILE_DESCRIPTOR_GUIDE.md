# 从第一版 VFS 到文件描述符表

上一轮我们已经做到了：

```text
shell
-> VFS
-> FileHandle / DirectoryHandle
-> OS64FS
-> BlockDevice
-> BootVolume
```

这已经比 shell 直接碰 inode 好很多。

但真实现代操作系统里，
应用通常不是直接拿一个 `VfsFile` 对象。

它拿到的是一个小整数：

```text
fd = 0
fd = 1
fd = 2
fd = 3
```

这个小整数就叫：

```text
file descriptor
文件描述符
```

这一轮补的是：

```text
FileDescriptorTable
fd_open
fd_read
fd_close
fd_stat
fd_seek
```

你可以先把它理解成：

> 内核里第一张“打开文件编号表”。

---

## 1. 为什么 VFS 后面还要做 fd

如果 shell 直接保存 `VfsFile`，
链路是：

```text
cat
-> VfsFile
-> FileHandle
-> OS64FS
```

这已经能工作，
但还不是现代 OS 常见形状。

真实系统调用通常长这样：

```c
int fd = open("readme.txt");
read(fd, buffer, size);
close(fd);
```

这里用户程序不会看到内核里的文件对象。

它只看到：

```text
一个整数 fd
```

然后内核内部做：

```text
fd
-> 当前进程的 fd 表
-> 打开的文件对象
-> VFS
-> 具体文件系统
```

所以这一轮的目标不是“让功能更多”，
而是把结构继续改得更像真实内核。

---

## 2. 新增了哪些文件

新增：

```text
kernel/fs/fd.hpp
kernel/fs/fd.cpp
```

它们属于 `fs/`，
因为 fd 表虽然以后会和进程绑定，
但它当前管理的是“打开文件对象”。

这轮还改了：

```text
kernel/shell/shell.hpp
kernel/shell/shell.cpp
kernel/core/kernel_main.cpp
scripts/build-stage1-image.sh
scripts/test-stage1.sh
scripts/test-invalid-opcode.sh
scripts/test-page-fault.sh
```

原因分别是：

- `shell` 要把 `cat` 改成通过 fd 读文件
- `kernel_main` 要加启动自测
- 构建脚本要把 `fd.cpp` 编译进内核
- 测试脚本要检查 fd 层日志

---

## 3. FileDescriptorTable 是什么

现在的结构是：

```cpp
struct FileDescriptorTable {
  const VfsMount* vfs;
  FileDescriptorEntry entries[16];
  uint32_t open_count;
};
```

小白版理解：

```text
FileDescriptorTable = 一张表
entries[0]          = 0 号 fd 槽位
entries[1]          = 1 号 fd 槽位
entries[2]          = 2 号 fd 槽位
...
```

如果 `fd_open("readme.txt")` 返回 `0`，
意思不是文件内容在数字 0 里面。

真正意思是：

```text
0
-> 去 fd 表的 entries[0]
-> 找到这个槽位里的 VfsFile
-> 再通过 VFS 读文件
```

这一轮容量先固定成：

```text
16
```

为什么不用动态扩容？

因为当前内核还在早期阶段，
固定数组更容易测试，也更不容易把调试复杂度拉爆。

---

## 4. FileDescriptorEntry 是什么

每个 fd 槽位现在长这样：

```cpp
struct FileDescriptorEntry {
  VfsFile file;
  bool open;
};
```

它表示：

```text
这个 fd 槽位背后有没有打开一个文件
如果有，真正打开的 VFS 文件对象是谁
```

所以：

```text
fd_is_open(table, 0)
```

本质上就是在问：

```text
0 号槽位是不是被占用了
0 号槽位里的 VfsFile 是不是还有效
```

---

## 5. fd_open 做了什么

`fd_open(table, path)` 的流程是：

```text
1. 检查 fd 表是否已经初始化
2. 检查 VFS 是否已经挂载
3. 从 entries[0] 开始找空槽位
4. 找到空槽位后调用 vfs_open_file
5. 打开成功才把槽位标记成 open
6. 返回这个槽位编号
```

所以第一次打开 `readme.txt`，
自测里应该得到：

```text
fd_open=0
```

因为 0 号槽位最先空着。

如果失败，
会返回：

```text
-1
```

也就是代码里的：

```text
kInvalidFileDescriptor
```

---

## 6. fd_read 做了什么

`fd_read(table, fd, buffer, size)` 的流程是：

```text
1. 检查 fd 是否在合法范围内
2. 检查这个槽位是否打开
3. 找到槽位背后的 VfsFile
4. 调用 vfs_read_file
5. 返回实际读到的字节数
```

注意：

```text
fd_read
```

自己不解析路径。

路径只在 `fd_open` 时解析一次。

后续读文件时，
只靠 fd 找到已经打开的文件对象。

这就是“打开文件”的意义：

```text
open 负责路径解析
read 负责从已打开对象继续读
```

---

## 7. fd_close 做了什么

`fd_close(table, fd)` 的流程是：

```text
1. 检查 fd 是否有效
2. 调用 vfs_close_file 关闭背后的 VFS 文件
3. 把这个 fd 槽位清零
4. open_count 减 1
```

现在关闭文件看起来很简单，
但接口很重要。

以后如果加：

- 进程
- 引用计数
- page cache
- 可写文件
- pipe
- socket

都需要有明确的关闭入口。

---

## 8. shell 的 cat 为什么改

以前 `cat` 是：

```text
cat
-> vfs_open_file
-> vfs_read_file
-> vfs_close_file
```

现在变成：

```text
cat
-> fd_open
-> fd_read
-> fd_close
```

输出没有故意改花哨，
仍然保持：

```text
cat_path=readme.txt
cat_size=72
os64fs readme: ...
```

原因是：

> 测试能继续确认用户看到的 shell 行为没变，但内部结构已经升级。

这也是写内核时很重要的习惯：

```text
外部行为尽量稳定
内部结构逐步变好
```

---

## 9. 启动自测新增了什么

内核启动时现在会多出：

```text
fd_table ok
fd_open=0
fd_read_total=72
fd_eof_read=0
fd_open_count=0
fd_layer ok
```

分别表示：

- `fd_table ok`
  fd 表初始化成功，而且已经连上 VFS
- `fd_open=0`
  第一次打开文件拿到了 0 号 fd
- `fd_read_total=72`
  通过 fd 读完了 `readme.txt`
- `fd_eof_read=0`
  文件读到末尾后，再读一次返回 0
- `fd_open_count=0`
  关闭文件后，打开文件数量回到 0
- `fd_layer ok`
  fd 的 open/read/seek/stat/close 路径全部通过

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

测试脚本现在会检查：

```text
fd_layer ok
```

这样可以确认 fd 层没有破坏后面的 timer、keyboard、shell 和异常路径。

---

## 11. 这一轮你真正学到了什么

这一步真正建立的是现代内核里很关键的一条线：

```text
路径
-> open
-> fd
-> read/write/close
```

现在这个项目里已经有了：

```text
shell
-> fd
-> fd table
-> VFS
-> FileHandle / DirectoryHandle
-> OS64FS
-> BlockDevice
-> BootVolume
```

虽然还没有用户态和系统调用，
但结构已经开始接近真实 OS。

---

## 12. 下一步最合理做什么

下一步最合理的是：

> 不急着做可写文件系统，先把 VFS 的路径和挂载结构继续整理。

可以继续往两个方向走：

- 做 `cwd` 当前工作目录，让 `cd docs`、`pwd`、相对路径更像真实 shell
- 做第一版 `open/read/close` 系统调用形状，为用户态进程做准备

如果继续走文件系统线，
我建议先做：

```text
cwd + cd/pwd
```

原因是它能继续锻炼 VFS 路径解析，
而且会让 shell 更像一个真正 OS 的调试终端。

这一轮已经继续落到了下一篇：

[从文件描述符表到 shell 当前工作目录](./KERNEL_SHELL_CWD_GUIDE.md)
