# 从文件句柄到目录句柄

上一轮我们做了 `FileHandle`：

```text
file_open
file_read
file_close
file_stat
```

这样 `cat` 和 `stat` 不再直接碰 `Os64FsInode`。

但还有一个地方没有改：

> `ls` 仍然直接读 `Os64FsDirEntry`。

这一轮补的是：

```text
DirectoryHandle
directory_open
directory_read
directory_close
```

也就是目录版的文件句柄。

---

## 1. 为什么 `ls` 也不应该直接碰目录项

`Os64FsDirEntry` 是 `OS64FS` 的底层目录项格式。

它里面保存：

- inode 编号
- 类型
- 名字长度
- 名字内容

如果 `ls` 直接使用它，
链路就是：

```text
ls
-> Os64FsDirEntry
-> OS64FS 内部格式
```

这和之前 `cat` 直接碰 inode 是同一个问题：

> shell 和底层文件系统格式绑死了。

所以现在把 `ls` 改成：

```text
ls
-> DirectoryHandle
-> OS64FS
```

这样以后底层文件系统换格式，
`ls` 不一定要大改。

---

## 2. 新增了哪些文件

新增：

```text
kernel/fs/directory.hpp
kernel/fs/directory.cpp
```

它们负责把底层目录项包装成上层更稳定的目录访问接口。

---

## 3. DirectoryEntry 是什么

`DirectoryEntry` 是给上层看的目录项。

它现在包含：

- `inode_number`
- `type`
- `name_length`
- `name`
- `size_bytes`

注意这里的 `DirectoryEntry` 不是磁盘上的原始结构。

它是：

```text
从 Os64FsDirEntry 转出来的上层结果
```

为什么还要多放一个 `size_bytes`？

因为 `ls` 输出时想看到：

```text
ls[0]=file readme.txt size=72
```

而原始目录项只知道名字指向哪个 inode，
不知道目标文件多大。

所以 `directory_read()` 内部会多做一步：

```text
读目录项
-> 拿到 inode_number
-> 再读目标 inode
-> 把 size_bytes 填到 DirectoryEntry
```

这样上层 `ls` 就不用自己再去读 inode。

---

## 4. DirectoryHandle 是什么

`DirectoryHandle` 表示：

> 已经打开的一个目录。

它里面有：

- `filesystem`
  这个目录来自哪个文件系统
- `inode`
  打开目录时找到的目录 inode
- `next_entry_index`
  下一次要读第几个目录项
- `entry_count`
  当前目录一共有多少项
- `open`
  当前句柄是否有效

它和 `FileHandle` 很像。

区别是：

```text
FileHandle      的 read 单位是字节
DirectoryHandle 的 read 单位是目录项
```

---

## 5. directory_open 做了什么

流程是：

```text
1. 检查文件系统是否挂载
2. 通过 os64fs_lookup_path 找到路径
3. 检查目标必须是目录
4. 计算这个目录有多少目录项
5. 初始化 DirectoryHandle
```

所以：

```text
directory_open(filesystem, "/", &handle)
```

打开的是根目录。

```text
directory_open(filesystem, "docs", &handle)
```

打开的是 `docs/` 子目录。

---

## 6. directory_read 做了什么

每调用一次：

```text
directory_read(handle, out_entry)
```

它就读取当前 `next_entry_index` 指向的目录项，
然后把 `next_entry_index` 自动加 1。

所以连续调用时，行为是：

```text
第 1 次 -> 读第 0 项
第 2 次 -> 读第 1 项
第 3 次 -> 读第 2 项
```

读到末尾后，
再调用会返回 `false`。

这和真实系统里的 `readdir()` 很像。

---

## 7. shell 的变化

以前 `ls` 的链路是：

```text
ls
-> os64fs_lookup_path
-> os64fs_directory_entry_count
-> os64fs_read_directory_entry
-> os64fs_read_inode
```

现在变成：

```text
ls
-> file_stat
-> directory_open
-> directory_entry_count
-> directory_read
-> directory_close
```

这里 `file_stat` 的作用是先区分：

- 路径不存在
- 路径存在但不是目录

然后真正列目录时，
`ls` 就只依赖目录句柄层。

---

## 8. 启动自测新增了什么

内核启动日志现在会多出：

```text
directory_open ok
directory_root_entries=3
directory_read_count=3
directory_rewind_index=0
directory_docs_first_inode=5
directory_layer ok
```

分别表示：

- `directory_open ok`
  成功打开根目录 `/`
- `directory_root_entries=3`
  根目录里有 3 项
- `directory_read_count=3`
  通过 `directory_read()` 顺序读完 3 项
- `directory_rewind_index=0`
  `directory_rewind()` 能把读取位置重置回 0
- `directory_docs_first_inode=5`
  打开 `docs/` 后读到的第一项是 inode 5，也就是 `guide.txt`
- `directory_layer ok`
  目录句柄整条链通过

---

## 9. 这一轮怎么测试

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
directory_layer ok
```

这保证目录句柄层在后续 timer、keyboard、shell、exception 之前已经正常通过。

---

## 10. 这一轮你真正学到了什么

现在文件系统上层已经更完整了：

```text
cat/stat -> FileHandle
ls      -> DirectoryHandle
```

也就是说：

> shell 不再直接依赖 OS64FS 的 inode 和目录项格式。

这就是继续走向 VFS 的准备动作。

---

## 11. 读完这一篇后继续看什么

这一篇解决的是：

```text
ls 不再直接碰 OS64FS 目录项
```

下一篇继续把 `ls` / `cat` / `stat` 都收口到统一的 VFS 入口：

[从文件/目录句柄到第一版 VFS](./KERNEL_VFS_GUIDE.md)
