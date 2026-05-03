# 从 shell cwd 到第一版系统调用形状

上一轮已经做到：

```text
shell input
-> cwd path resolver
-> absolute path
-> fd table
-> VFS
-> OS64FS
-> BlockDevice
-> BootVolume
```

这说明内核已经有了：

- 路径解析
- 文件描述符
- VFS
- 文件读取

所以这一轮最合理的下一步不是马上做新文件系统功能，
而是先补一层：

```text
sys_open
sys_read
sys_stat
sys_seek
sys_close
```

你可以把它理解成：

> 先把“系统调用应该长什么样”搭起来。

---

## 1. 先说清楚：这还不是 CPU syscall 指令

现代 x86_64 CPU 里确实有一条指令叫：

```asm
syscall
```

它负责让用户态程序从低权限进入内核态。

但这一轮还没有做：

- 用户态 ring 3
- 进程地址空间
- `syscall` / `sysret` 汇编入口
- 寄存器传参 ABI
- 内核态和用户态栈切换
- 用户指针安全检查

所以当前的 `kernel/syscall/` 更准确地说是：

```text
系统调用外观
```

它先做接口形状：

```text
open(path) -> fd 或负数错误码
read(fd)  -> 字节数、EOF 0 或负数错误码
close(fd) -> 状态码
```

等以后真正有用户态后，
CPU 的 `syscall` 入口会在更底层接住寄存器，
然后再转进这一层。

---

## 2. 为什么现在要做这一层

如果没有 syscall 层，
上层代码可能会直接乱调：

```text
shell
-> fd_open
-> vfs_open_file
-> file_open
-> os64fs_lookup_path
```

这样虽然能跑，
但以后加用户态进程时会很痛苦。

真实 OS 里，
用户程序一般不是直接碰 VFS 对象，
而是这样：

```c
int fd = open("/docs/guide.txt");
read(fd, buffer, size);
close(fd);
```

用户程序只知道：

```text
路径
fd 小整数
返回值
错误码
```

它不应该知道：

```text
VfsFile
FileHandle
Os64FsInode
BootVolume
```

所以这一层的意义是：

> 把“内核内部复杂对象”挡在系统调用边界里面。

---

## 3. 新增了哪些文件

新增：

```text
kernel/syscall/syscall.hpp
kernel/syscall/syscall.cpp
```

它们没有放进 `fs/`，
因为 syscall 不是某一个文件系统的一部分。

它的位置更靠上：

```text
syscall
-> fd table
-> VFS
-> FileHandle / DirectoryHandle
-> OS64FS
-> BlockDevice
-> BootVolume
```

这一轮还改了：

```text
kernel/core/kernel_main.cpp
scripts/build-stage1-image.sh
scripts/test-stage1.sh
scripts/test-invalid-opcode.sh
scripts/test-page-fault.sh
README.md
kernel/README.md
docs/README.md
docs/KERNEL_SHELL_CWD_GUIDE.md
```

原因分别是：

- `kernel_main` 要启动自测 syscall 层
- 构建脚本要把 `syscall.cpp` 编译并链接进内核
- 测试脚本要检查 syscall 层日志
- 文档要写清楚为什么这样分层

---

## 4. SyscallContext 是什么

现在结构很小：

```cpp
struct SyscallContext {
  FileDescriptorTable* fd_table;
};
```

小白版理解：

```text
SyscallContext = 一次系统调用执行时，内核需要知道的上下文
```

当前还没有进程，
所以它只保存：

```text
全局 fd 表
```

以后有进程后，
这里会变成类似：

```text
当前进程
当前进程的 fd 表
当前进程的地址空间
当前权限信息
```

也就是说，
这一轮看起来只是包了一层指针，
但它是在为以后“每个进程有自己的系统调用上下文”占位置。

---

## 5. 为什么要有统一错误码

`fd_open` 失败时原来只返回：

```text
-1
```

这对底层 fd 表够用，
但对系统调用层不够。

因为上层需要知道失败大概是什么类型：

```text
路径不存在
路径是目录，不是文件
fd 是坏的
参数本身不合法
底层 I/O 失败
```

所以这一轮定义了：

```cpp
enum SyscallStatus : int32_t {
  kSyscallOk = 0,
  kSyscallInvalidArgument = -1,
  kSyscallNotFound = -2,
  kSyscallNotFile = -3,
  kSyscallBadFileDescriptor = -4,
  kSyscallIoError = -5,
};
```

为什么错误码是负数？

因为 `read` 成功时要返回：

```text
读到了多少字节
```

这是非负数。

于是可以用：

```text
>= 0 表示成功结果
< 0  表示失败错误码
```

这和很多真实系统调用的风格很接近。

---

## 6. sys_open 做了什么

`sys_open(context, path)` 的流程是：

```text
1. 检查 context 是否有效
2. 检查 path 是否为空
3. 先通过 vfs_stat 看路径是否存在
4. 如果路径不存在，返回 kSyscallNotFound
5. 如果路径存在但不是普通文件，返回 kSyscallNotFile
6. 再调用 fd_open 分配一个 fd
7. 成功返回 fd，失败返回 kSyscallIoError
```

为什么 open 前还要先 stat？

因为底层 `fd_open` 现在只知道：

```text
打开成功 / 打开失败
```

但 syscall 层想给上层更清楚的错误。

所以它先问 VFS：

```text
这个路径到底是什么？
```

再决定错误码。

---

## 7. sys_read 做了什么

`sys_read(context, fd, buffer, bytes_to_read)` 的流程是：

```text
1. 检查 context 是否有效
2. 如果要读 0 字节，直接返回 0
3. 检查 buffer 是否为空
4. 检查 fd 是否真的打开
5. 调用 fd_read
6. 返回实际读到的字节数
```

这里有一个重要规则：

```text
EOF 不是错误
```

EOF 的意思是：

```text
文件读完了
```

所以 EOF 返回：

```text
0
```

而不是负数。

这也是为什么 `read` 的返回值设计成：

```text
> 0 读到字节
= 0 EOF 或请求读 0 字节
< 0 错误
```

---

## 8. sys_stat / sys_seek / sys_close 做了什么

这三个函数都是在 fd 表上再包一层。

```text
sys_stat
-> 检查 fd
-> fd_stat
-> 返回 VfsStat
```

```text
sys_seek
-> 检查 fd
-> fd_seek
-> 修改文件读取偏移
```

```text
sys_close
-> 检查 fd
-> fd_close
-> 释放 fd 表槽位
```

它们看起来很薄，
但意义是：

> 上层以后不需要知道 fd 表函数叫什么，只需要走 sys_* 入口。

---

## 9. 启动自测做了什么

`kernel_main.cpp` 新增了：

```text
run_syscall_smoke_test
```

它会实际做：

```text
1. 初始化 SyscallContext
2. sys_open("/docs/guide.txt")
3. sys_stat(fd)
4. sys_read(fd) 分多次读完整个 guide.txt
5. 再读一次，确认 EOF 返回 0
6. sys_seek(fd, 0)
7. 再读开头几个字节，确认 seek 生效
8. sys_close(fd)
9. 检查 fd 表 open_count 回到 0
```

启动日志会出现：

```text
syscall_context ok
sys_open=0
sys_stat_inode=5
sys_read_total=193
sys_eof_read=0
sys_open_count=0
syscall_layer ok
```

这些日志分别证明：

- syscall 上下文初始化成功
- `open` 返回了第一个 fd：0
- `stat` 看到的是 `/docs/guide.txt` 的 inode 5
- `read` 读完了 193 字节的 guide 文本
- 文件读完以后再读会返回 EOF 0
- `close` 后 fd 表没有泄漏打开文件
- 整个 syscall 外观层跑通

---

## 10. 现在的完整读取路径

现在读 `/docs/guide.txt` 的链路可以写成：

```text
sys_open("/docs/guide.txt")
-> fd_open
-> vfs_open_file
-> file_open
-> os64fs_lookup_path
-> inode 5
```

真正读内容时：

```text
sys_read(fd)
-> fd_read
-> vfs_read_file
-> file_read
-> os64fs_read_inode
-> block_device_read_sector
-> boot volume memory
```

这就是现代内核常见的分层思路：

```text
系统调用边界
-> 进程/文件描述符层
-> VFS
-> 具体文件系统
-> 块设备
```

---

## 11. 这一轮你真正学到了什么

这一轮不是为了“多一个命令”。

真正学的是：

```text
系统调用不是业务逻辑本身，而是用户态和内核态之间的边界形状。
```

现在虽然还没有用户态，
但内核已经开始把边界整理出来：

```text
路径、fd、buffer、size、返回值、错误码
```

这些东西以后会成为用户程序和内核通信的基础。

---

## 12. 下一步最合理做什么

下一步有两条路：

第一条是继续做 syscall 更完整：

```text
sys_getcwd
sys_chdir
sys_listdir
sys_write 的设计占位
```

第二条是开始准备真正用户态：

```text
用户态 ring 3
TSS / 内核栈
syscall/sysret 或 int 0x80 入口
寄存器 ABI
用户指针检查
```

对当前项目来说，
更稳的顺序是：

```text
先继续补 syscall API
再做最小用户态入口
```

原因是：

> 先把内核服务边界稳定下来，再让用户程序进入内核，调试成本会低很多。

继续阅读：

```text
docs/KERNEL_SYSCALL_CWD_GUIDE.md
```
