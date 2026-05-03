# 从原始块设备到第一版只读文件系统

> 注意：
> 这篇文档讲的是“第一版只读 `OS64FS v1` 是怎么搭起来的”。
> 如果你想看当前代码里已经升级后的位图、一致性校验和 `OS64FSV3` 布局，请继续看 [KERNEL_OS64FS_V3_GUIDE.md](./KERNEL_OS64FS_V3_GUIDE.md)。

这一步开始，你的内核不再只是“能读某个扇区”，
而是正式跨进：

> 能理解目录、路径、文件内容。

这一轮实现的是：

- `BlockDevice`
- 只读 `OS64FS v1`
- shell 里的 `ls`
- shell 里的 `cat <path>`
- shell 里的 `stat <path>`
- 路径里的 `/`、`.`、`..`

而且它们都已经接进自动测试。

---

## 1. 为什么这一步先做“只读文件系统”

因为你现在最需要先学会的是：

1. 文件系统镜像在磁盘上怎么布局
2. 内核怎么把一段块数据解释成 superblock / inode / 目录项
3. 路径怎么一级一级走到目标文件
4. 文件内容怎么从数据块里读出来

如果一开始就做“可写文件系统”，
你会立刻撞上另一批更复杂的问题：

- 空闲块管理
- 写回顺序
- 崩溃一致性
- 元数据更新
- 回收和碎片

这些都重要，
但它们不适合作为“小白第一次把内核文件系统写起来”的入口。

所以这一轮故意先做：

> 只读、可观察、结构简单、容易验证。

---

## 2. 这一轮的真实链路是什么

现在启动后，数据流是这样走的：

```text
disk image
-> stage2 先把 boot volume 预读到内存
-> BootInfo 把这段内存位置交给 64 位内核
-> BootVolume 表示“原始连续扇区”
-> BlockDevice 提供统一 sector read 接口
-> OS64FS 挂载这个块设备
-> shell 用 ls/cat/stat 访问文件系统
```

最关键的变化是：

> `BootVolume` 不再是“自定义卷头格式”，而是“原始块区”。

这样做的好处是，
以后底层如果换成：

- 真正的 ATA 驱动
- AHCI 驱动
- 更现代的启动器交接块设备

上层文件系统代码都不用跟着重写。

---

## 3. 为什么中间还要多一层 BlockDevice

很多新手第一次写文件系统时会直接把文件系统代码绑死到某个底层实现上。

比如：

```text
filesystem 直接调用 boot_volume_read_sector()
```

这会带来一个问题：

> 文件系统和“当前这一个特殊启动来源”绑死了。

这一轮专门补了一层：

```text
BlockDevice
```

它只关心 4 件事：

- 起始 LBA
- 扇区数
- 扇区大小
- `read_sector`

所以对 `OS64FS` 来说，
底层来源已经被统一成：

> “给我一个可按扇区读取的块设备。”

这就是为什么工程上要先做抽象层。

---

## 4. OS64FS v1 的磁盘布局

这一轮文件系统镜像还是很小，
一共只用了 4 个扇区，每个扇区 512 字节。

布局是：

```text
sector 0 : superblock
sector 1 : inode table
sector 2 : data area
sector 3 : data area
```

但这里有一个重要设计：

> 扇区大小还是 512 字节，文件系统自己的逻辑数据块改成了 128 字节。

原因是：

- 扇区是底层设备天然单位
- 文件系统不一定要用和扇区一样大的“文件数据块”

如果也用 512 字节做最小文件块，
那你一个 30 字节的小文件就要浪费整整一个扇区。

所以 v1 先取：

```text
data_block_size = 128
```

这样一个 512 字节扇区里可以放 4 个小数据块。

---

## 5. superblock 里放了什么

superblock 可以理解成：

> 整个文件系统的总说明书。

当前里面最重要的字段有：

- 签名 `OS64FSV1`
- 版本号
- 总扇区数
- inode 表起始扇区
- inode 数量
- inode 大小
- 数据区起始扇区
- 数据区扇区数
- 逻辑数据块大小
- 根目录 inode 号
- 卷名

内核挂载时先读它，
确认：

- 这是不是我们自己的文件系统
- inode 表应该去哪读
- 后面的数据块应该怎么解释

---

## 6. inode 是什么

inode 不保存“路径”，
它保存的是：

> 一个文件对象本身的元数据。

这一轮 inode 里最重要的是：

- inode 编号
- 类型：文件还是目录
- 链接计数
- 大小
- 4 个 direct block

其中 `direct_blocks[4]` 的意思是：

> 这个文件最多先直接指向 4 个逻辑数据块。

第一版故意不做：

- 间接块
- 双重间接块
- extent

因为现在最重要的是先把最基础模型看懂。

---

## 7. 目录为什么也能当“文件”处理

这是文件系统里一个很重要的观念：

> 目录本质上也是一个文件，只不过它里面存的不是普通文本，而是一串目录项。

所以这一轮目录 inode 的 `size_bytes`，
表示的不是“文字长度”，
而是：

> 这一片目录项区域一共有多少字节。

然后每个目录项 `Os64FsDirEntry` 里保存：

- 指向哪个 inode
- 这个名字是 file 还是 dir
- 名字长度
- 名字本身

于是路径查找就能这样做：

```text
root
-> 找到 "docs"
-> 进入 docs 对应的 inode
-> 在 docs 目录里找到 "guide.txt"
```

---

## 8. 这一轮镜像里实际放了什么

当前镜像里放了这些内容：

根目录：

- `readme.txt`
- `notes.txt`
- `docs/`

子目录：

- `docs/guide.txt`

这里故意让 `guide.txt` 跨了两个 128 字节数据块，
目的不是为了“显得复杂”，
而是为了验证：

> 文件读取逻辑已经不只会读单块文件。

也就是说，
现在内核已经真的会：

- 计算当前读到文件的第几个逻辑块
- 找到这个逻辑块落在哪个扇区、哪个偏移
- 把跨块文件继续拼起来

---

## 9. shell 现在学会了什么

### `ls`

作用：

```text
列目录项
```

比如：

```text
ls
```

它会列出根目录里的：

- `readme.txt`
- `notes.txt`
- `docs`

也可以直接列子目录：

```text
ls docs
```

这会列出：

- `guide.txt`

### `cat <path>`

作用：

```text
打印文件内容
```

比如：

```text
cat readme.txt
```

现在也支持从根目录开始的绝对路径：

```text
cat /docs/guide.txt
```

### `stat <path>`

作用：

```text
查看 inode 元数据
```

比如：

```text
stat docs/guide.txt
```

路径里也可以使用 `..` 回到上一层：

```text
stat docs/../notes.txt
```

你会看到：

- inode 编号
- 类型
- 大小
- 链接数
- 使用了几个 direct block

---

## 10. 为什么 `stat` 很重要

很多新手会觉得：

> `cat` 能看到内容就够了。

其实不是。

`cat` 只能证明“最后读出来的字符串对了”，
但 `stat` 能帮你看到：

- 这个路径最后落到了哪个 inode
- 它到底被当成 file 还是 dir
- 它跨了几个数据块

所以 `stat` 是非常重要的调试命令。

---

## 11. 路径里的 `.` 和 `..` 是怎么处理的

这一轮补了最小路径语义：

- `/docs/guide.txt`
- `./readme.txt`
- `docs/../notes.txt`
- `ls docs`

实现方式不是在镜像目录里强行写入 `.` 和 `..` 两个目录项，
而是在 `os64fs_lookup_path()` 里解释这两个特殊路径组件。

你可以把它理解成：

```text
.  = 留在当前目录
.. = 回到上一层目录
```

因为现在还没有“每个进程自己的当前工作目录”，
所以 shell 输入的相对路径仍然默认从根目录开始解析。

也就是说：

```text
docs/guide.txt
```

当前等价于：

```text
/docs/guide.txt
```

这不是最终 POSIX 行为，
而是当前内核还没有进程和工作目录之前的合理过渡设计。

---

## 12. 这一轮怎么测试

### 正常启动回归

运行：

```bash
make test-stage1
```

现在它会额外检查这些关键日志：

- `boot volume ok`
- `filesystem ok`
- `os64fs_signature=OS64FSV1`
- `shell_line=ls`
- `shell_line=ls docs`
- `shell_line=cat readme.txt`
- `shell_line=cat /docs/guide.txt`
- `shell_line=stat docs/guide.txt`
- `shell_line=stat docs/../notes.txt`

### 异常回归

运行：

```bash
make test-invalid-opcode
make test-page-fault
```

这样可以确认：

> 文件系统路径层这轮改动没有把原来的 trap / exception 路径带坏。

---

## 13. 这一轮你真正学到了什么

如果你能把这一步看懂，
你就已经开始接触操作系统里一条非常真实的链：

```text
块设备
-> superblock
-> inode
-> directory entry
-> path lookup
-> file read
```

这条链就是很多真实文件系统的骨架。

虽然现在还是极小版本，
但它已经不再是“假装有文件系统”，
而是：

> 真的有目录、真的有路径、真的能读文件。

---

## 14. 读完这一篇后继续看什么

这一篇讲到的是：

```text
BlockDevice
-> OS64FS
-> inode / directory entry / path lookup / read inode data
```

下一篇继续把它往上包一层：

```text
OS64FS
-> FileHandle
-> file_open / file_read / file_close / file_stat
```

也就是：

> 不再让 shell 直接碰 inode，而是让 shell 使用更像真实内核的文件句柄接口。

继续看：

[从只读文件系统到内核文件句柄层](./KERNEL_FILE_HANDLE_GUIDE.md)

一句话总结这一步：

> 你现在的 64 位内核，已经从“能读扇区”升级成“能理解文件”。 
