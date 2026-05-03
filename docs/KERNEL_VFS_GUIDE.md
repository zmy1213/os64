# 从文件/目录句柄到第一版 VFS

前面我们已经做了两层：

```text
FileHandle      -> 给 cat/stat 用
DirectoryHandle -> 给 ls 用
```

这一步继续往上加一层：

```text
VFS
```

VFS 的全称是：

```text
Virtual File System
```

可以先理解成：

> 具体文件系统之上的统一入口。

现在这个项目里只有一个具体文件系统：

```text
OS64FS
```

但真实现代内核里会有很多种文件系统，
比如：

- ext4
- FAT
- tmpfs
- procfs
- devfs

如果 shell、进程、系统调用都直接依赖某一个具体文件系统，
系统会很难扩展。

所以现代内核通常会加一层 VFS。

---

## 1. 这一轮为什么要做 VFS

前面两轮已经把 shell 和底层格式拆开了一部分：

```text
cat/stat -> FileHandle
ls       -> DirectoryHandle
```

但 shell 还是知道：

```text
FileHandle
DirectoryHandle
```

而且这两套接口还是直接围着 `OS64FS` 长出来的。

这一轮继续往上收口：

```text
shell
-> VFS
-> FileHandle / DirectoryHandle
-> OS64FS
-> BlockDevice
```

也就是说：

> shell 先只认识 VFS，不再直接调用 file_* / directory_*。

这是理解现代内核文件系统结构的重要一步。

---

## 2. 新增了哪些文件

新增：

```text
kernel/fs/vfs.hpp
kernel/fs/vfs.cpp
```

这一层现在先提供：

```text
initialize_vfs
vfs_stat
vfs_open_file
vfs_read_file
vfs_close_file
vfs_open_directory
vfs_read_directory
vfs_close_directory
```

现在它内部仍然转给：

```text
file_*
directory_*
```

但上层已经不需要直接知道这些底层细节。

---

## 3. VfsMount 是什么

`VfsMount` 表示：

> 一个挂载进 VFS 的文件系统入口。

当前结构很简单：

```text
VfsMount
-> OS64FS
```

也就是：

```text
g_vfs
-> g_os64fs
```

为什么不直接让 shell 拿 `g_os64fs`？

因为如果以后变成：

```text
/
-> OS64FS

/dev
-> devfs

/tmp
-> ramfs
```

shell 不应该自己判断每个路径该去哪个文件系统。

这个判断应该属于 VFS。

---

## 4. VfsStat 是什么

`VfsStat` 是 VFS 层统一的元数据结果。

它和之前的 `FileStat` 很像，
但含义不同：

```text
FileStat = file 层给出的结果
VfsStat  = VFS 层给上层看的结果
```

现在 `VfsStat` 里仍然保留：

- inode 编号
- 类型
- 大小
- direct block

这是为了教学和 `stat` 调试方便。

以后如果继续抽象，
`direct block` 这种底层字段可以慢慢从 VFS 结果里拿掉，
或者变成更通用的 debug 信息。

---

## 5. shell 的变化

以前：

```text
ls
-> directory_open
-> directory_read

cat
-> file_open
-> file_read

stat
-> file_stat
```

现在：

```text
ls
-> vfs_open_directory
-> vfs_read_directory

cat
-> vfs_open_file
-> vfs_read_file

stat
-> vfs_stat
```

所以 shell 的依赖方向变成：

```text
shell
-> VFS
```

这就是这一步最重要的变化。

---

## 6. 现在的真实层次

当前从 shell 到启动介质的完整链路是：

```text
shell command
-> VFS
-> FileHandle / DirectoryHandle
-> OS64FS
-> BlockDevice
-> BootVolume
-> stage2 预读进来的扇区
```

每一层负责的事情不同：

- `shell`
  只负责命令语义，比如 `ls` / `cat` / `stat`
- `VFS`
  负责给上层统一入口
- `FileHandle`
  负责打开文件和按 offset 读字节
- `DirectoryHandle`
  负责打开目录和顺序读目录项
- `OS64FS`
  负责理解 superblock / inode / dir entry / direct block
- `BlockDevice`
  负责按扇区读
- `BootVolume`
  负责表示 stage2 预读到内存里的原始块区域

---

## 7. 启动自测新增了什么

内核启动时现在会多出：

```text
vfs_mount ok
vfs_stat_inode=5
vfs_file_read_total=72
vfs_directory_entries=3
vfs_directory_first_inode=2
vfs_layer ok
```

分别表示：

- `vfs_mount ok`
  成功把 `OS64FS` 挂进 VFS
- `vfs_stat_inode=5`
  通过 `vfs_stat("/docs/guide.txt")` 找到了 inode 5
- `vfs_file_read_total=72`
  通过 `vfs_open_file` / `vfs_read_file` 读完了 `readme.txt`
- `vfs_directory_entries=3`
  通过 `vfs_open_directory("/")` 看到了根目录 3 项
- `vfs_directory_first_inode=2`
  通过 VFS 读到根目录第一项 `readme.txt`
- `vfs_layer ok`
  VFS 的 stat / file read / directory read 全部通过

---

## 8. 这一轮怎么测试

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
vfs_layer ok
```

这样可以确认 VFS 层没有破坏后面的 timer、keyboard、shell 和异常路径。

---

## 9. 这一轮你真正学到了什么

这一步不是为了“多包一层显得高级”。

它解决的是一个真实内核问题：

> 上层系统不应该依赖某一个具体文件系统。

现在 shell 不再直接依赖：

```text
OS64FS
FileHandle
DirectoryHandle
```

而是依赖：

```text
VFS
```

这就是后面继续做：

- 文件描述符
- 进程打开文件表
- 系统调用 `open/read/close`
- 多文件系统挂载

的基础。

---

## 10. 下一步最合理做什么

下一步最合理的是：

> 做第一版文件描述符表。

也就是从：

```text
shell 直接调用 vfs_open_file
```

继续往前变成：

```text
fd = open(path)
read(fd, buffer)
close(fd)
```

先不用做用户态系统调用，
可以先在内核里做：

```text
FileDescriptorTable
```

这样以后进程和 syscall 出现时，
它们就有地方接了。

这一轮已经继续落到了下一篇：

[从第一版 VFS 到文件描述符表](./KERNEL_FILE_DESCRIPTOR_GUIDE.md)
