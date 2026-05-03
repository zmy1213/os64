# 从只读文件系统到内核文件句柄层

上一轮 `OS64FS` 已经能做到：

- 挂载 superblock
- 读取 inode 表
- 解析目录项
- 按路径找到文件
- 从 direct block 里读出文件内容

但还有一个问题：

> shell 仍然直接拿 `Os64FsInode` 读文件。

这在教学小内核里能跑，
但不是一个好结构。

这一轮补的是：

> `FileHandle` 文件句柄层。

也就是先做出内核内部版的：

```text
open
read
close
stat
```

---

## 1. 为什么不能一直让 shell 直接碰 inode

`inode` 是文件系统的底层元数据。

你可以把它理解成：

> 文件系统内部用来描述文件的结构。

它里面有：

- inode 编号
- 类型
- 文件大小
- direct block

这些信息很底层。

如果 shell 直接操作 inode，
就会出现一个问题：

```text
shell
-> 直接依赖 OS64FS 的 inode 格式
```

以后如果你换文件系统，
比如：

- `OS64FS v2`
- FAT
- ext2 风格文件系统
- 内存文件系统 ramfs

shell 就要跟着改。

所以这一轮开始把结构改成：

```text
shell
-> FileHandle
-> OS64FS
-> BlockDevice
-> BootVolume
```

这样 shell 只需要知道：

```text
我能打开一个路径
我能从句柄里读字节
我能拿到 stat 信息
我能关闭它
```

至于这个文件底下到底是 direct block，
还是以后变成 extent / page cache / 真实磁盘驱动，
shell 不应该关心。

---

## 2. 这一轮新增了哪些文件

新增：

```text
kernel/fs/file.hpp
kernel/fs/file.cpp
```

它们现在属于 `fs/`，
但职责不是“定义磁盘格式”，
而是：

> 在具体文件系统之上，提供更像真实内核的文件访问入口。

---

## 3. FileStat 是什么

`FileStat` 是给上层看的元数据快照。

它现在包含：

- `inode_number`
- `type`
- `link_count`
- `size_bytes`
- `direct_blocks[4]`

为什么不是直接把 `Os64FsInode` 扔给上层？

因为 `Os64FsInode` 是 `OS64FS` 的内部格式。

`FileStat` 是上层接口。

现在它们字段很像，
但意义不同：

```text
Os64FsInode = 底层磁盘格式对象
FileStat    = 上层查询结果对象
```

这一层区分越早建立，
后面越容易扩展。

---

## 4. FileHandle 是什么

`FileHandle` 表示：

> 已经打开的一个文件。

它里面现在有：

- `filesystem`
  这个文件来自哪个已挂载的 `OS64FS`
- `inode`
  打开时找到的 inode 副本
- `offset`
  下一次 read 从文件的哪个字节开始
- `open`
  这个句柄当前是否有效

最关键的是 `offset`。

没有文件句柄时，
每次读文件都要自己传：

```text
从 offset = 0 开始读
从 offset = 64 开始读
从 offset = 128 开始读
```

有了句柄后，
上层只需要不断调用：

```text
file_read(handle, buffer, size)
```

`file_read()` 会自己把 offset 往后推进。

这就更像真实操作系统里的文件读取模型。

---

## 5. file_open 做了什么

`file_open(filesystem, path, out_handle)` 的流程是：

```text
1. 检查文件系统是否已经挂载
2. 通过 os64fs_lookup_path 找到路径对应的 inode
3. 检查这个 inode 必须是普通文件
4. 把 filesystem / inode / offset / open 写进 FileHandle
```

这一版有一个故意保守的限制：

> `file_open` 只能打开普通文件，不能打开目录。

原因是目录读取需要另一套接口，
比如以后可能会做：

```text
opendir
readdir
closedir
```

现在先不混在一起。

---

## 6. file_read 做了什么

`file_read(handle, buffer, bytes_to_read)` 的流程是：

```text
1. 检查 handle 是否有效
2. 如果 offset 已经到文件末尾，返回 0
3. 如果用户想读太多，就自动裁剪到文件剩余长度
4. 调用 os64fs_read_inode_data 读取底层数据
5. 成功后推进 handle->offset
6. 返回本次实际读到的字节数
```

这里有一个真实系统里也很重要的语义：

> 读到 EOF 不算错误，返回 0。

所以这一轮测试里专门检查：

```text
file_eof_read=0
```

意思是：

> 文件读完之后，再读一次，应该返回 0。

---

## 7. file_close 现在为什么看起来很简单

现在的 `file_close()` 只是把句柄清零。

因为当前还没有：

- 引用计数
- page cache
- 打开的文件表
- 进程文件描述符表
- 写回缓存

所以暂时没有复杂资源要释放。

但接口必须先立起来。

原因是后面一旦你做：

```text
process fd table
```

就会自然变成：

```text
fd
-> open file object
-> FileHandle / VNode
-> filesystem
```

如果现在 shell 已经只依赖 `file_close()`，
以后底层加复杂逻辑时，
shell 不用大改。

---

## 8. shell 的变化

### cat

以前 `cat` 的链路是：

```text
cat
-> os64fs_lookup_path
-> Os64FsInode
-> os64fs_read_inode_data
```

现在变成：

```text
cat
-> file_stat
-> file_open
-> file_read
-> file_close
```

也就是说：

> `cat` 不再直接读 inode 数据。

### stat

以前 `stat` 的链路是：

```text
stat
-> os64fs_lookup_path
-> Os64FsInode
```

现在变成：

```text
stat
-> file_stat
-> FileStat
```

这样 `stat` 仍然能看目录，
比如以后输入：

```text
stat docs
```

这种语义是合理的。

---

## 9. 这一轮启动自测新增了什么

内核启动时现在会额外做文件句柄层自测：

```text
file_open ok
file_read_total=72
file_eof_read=0
file_stat_inode=5
file_layer ok
```

每一行分别表示：

- `file_open ok`
  成功通过路径打开了 `readme.txt`
- `file_read_total=72`
  通过多次 `file_read()` 读完了整个 `readme.txt`
- `file_eof_read=0`
  文件读完后继续读，正确返回 EOF
- `file_stat_inode=5`
  通过 `file_stat("/docs/guide.txt")` 找到了 inode 5
- `file_layer ok`
  open/read/seek/stat/close 整条链都通过

这里还顺便测试了：

```text
file_seek(handle, 0)
```

它证明句柄里的 offset 可以被重置。

---

## 10. 这一轮怎么测试

先构建：

```bash
make stage1
```

再跑正常启动回归：

```bash
make test-stage1
```

异常回归也要继续跑：

```bash
make test-invalid-opcode
make test-page-fault
```

原因是文件句柄层虽然是文件系统方向，
但它被放进了内核启动自测路径。

所以必须确认：

> 新的文件层没有破坏后面的 timer / keyboard / shell / exception 路径。

---

## 11. 这一轮你真正学到了什么

这一轮不是为了多写几个函数名。

真正重点是：

> 内核上层模块不应该直接依赖底层磁盘格式。

现在你已经有了更清晰的层次：

```text
shell command
-> file handle API
-> OS64FS path/inode/data API
-> block device API
-> boot volume raw sectors
```

这就是后面继续做 VFS、文件描述符、进程、系统调用的基础。

---

## 12. 读完这一篇后继续看什么

这一篇解决的是：

```text
cat/stat 不再直接碰 inode
```

下一篇继续解决：

```text
ls 不再直接碰目录项
```

继续看：

[从文件句柄到目录句柄](./KERNEL_DIRECTORY_HANDLE_GUIDE.md)
