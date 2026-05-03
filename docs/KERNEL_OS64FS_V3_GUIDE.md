# 从第一版只读文件系统到带位图和一致性校验的 OS64FS v3

这一步不是“又多读了几个文件”那么简单。

它真正做的升级是：

> 把 `OS64FS` 从“教学型只读布局”推进成“已经有正式元数据骨架的只读文件系统”。

这一轮最核心的变化有 4 个：

1. 文件系统签名从 `OS64FSV2` 升到 `OS64FSV3`
2. 磁盘布局里新增 `inode bitmap` 和 `data bitmap`
3. 挂载时会做一次最小一致性校验
4. shell 的 `disk` 命令开始能看到 inode / data block 的容量统计

这篇文档重点解释：

- 为什么要从前一版继续升级
- `OS64FS v3` 现在的真实磁盘布局是什么
- 挂载时到底在检查什么
- 这一步离“可写文件系统”还有多远

---

## 1. 为什么还要再升一级

前面的 `OS64FS v1 / v2` 已经能做到：

- 挂载文件系统
- 解析路径
- 列目录
- 读文件
- 支持更大的 inode / 目录项
- 支持 `8` 个 direct block 和 `1` 个 single indirect block

但它还有一个很明显的问题：

> 内核虽然知道“一个 inode 指向哪些块”，却还不知道“整个卷里哪些 inode 和哪些 data block 已经被占用”。

这会直接卡住后面的很多事情。

比如你以后想做：

- `create`
- `mkdir`
- `write`
- `append`
- `unlink`

你都必须先回答两个问题：

1. 新文件该拿哪个空闲 inode
2. 新数据该写进哪个空闲 data block

而这两个问题，本质上都要求：

> 文件系统必须显式维护“空闲 / 已占用”元数据。

所以这一轮先不急着做写支持，
而是先把更接近正式系统的元数据骨架立起来。

---

## 2. 这一轮到底升级了什么

可以把这次变化理解成：

```text
OS64FS v2
= 已经会读文件、读目录、支持单级间接块

OS64FS v3
= 在 v2 基础上，再补上“全卷资源分配视角”
```

具体来说：

### 2.1 superblock 变大了

以前 superblock 更像：

> “告诉你 inode 表和数据区在哪”

现在 superblock 还会继续告诉你：

- inode 位图在哪
- data 位图在哪
- 还剩多少空闲 inode
- 还剩多少空闲 data block

也就是说，
superblock 不再只是“布局说明书”，
还开始带一点“容量状态摘要”的味道。

### 2.2 新增了两张位图

这次新加的是：

- `inode bitmap`
- `data bitmap`

它们的职责分别是：

- `inode bitmap`：哪个 inode 已经分配，哪个还空着
- `data bitmap`：哪个 data block 已经被内容占用，哪个还空着

这就是正式文件系统里最基本的一类“资源分配表”。

### 2.3 挂载不再只是“能读就算成功”

以前挂载更像：

> superblock 看起来像样，inode 表也能读出来，那就先挂上去。

现在挂载会更严格：

1. 先读 superblock
2. 检查布局字段彼此是否自洽
3. 读 inode bitmap
4. 读 data bitmap
5. 读 inode table
6. 再反向扫描所有已分配 inode，看看它们实际占用了哪些块
7. 把“位图声称的占用情况”和“inode 真实引用的占用情况”对比

如果对不上，就直接拒绝挂载。

这已经很像一个最小版的：

> mount-time consistency check

它还不是完整 `fsck`，
但方向已经是正式文件系统的方向了。

---

## 3. OS64FS v3 现在的真实布局

当前这份教学镜像还是放在 `boot volume` 里，
由 [scripts/build-stage1-image.sh](/Users/zhuzhumingyang/githubProjects/aiProjects/os64/scripts/build-stage1-image.sh:1) 负责生成。

真实布局现在是：

```text
sector 0   : superblock
sector 1   : inode bitmap
sector 2   : data bitmap
sector 3-6 : inode table
sector 7+  : data area
```

当前关键参数是：

- `BOOT_VOLUME_SECTORS = 128`
- `inode_count = 32`
- `inode_size = 64`
- `directory_entry_size = 64`
- `data_block_size = 512`
- `root_inode = 1`

所以整个卷现在是：

```text
1 个 superblock
+ 1 个 inode bitmap sector
+ 1 个 data bitmap sector
+ 4 个 inode table sector
+ 121 个 data sector
```

注意这里有一个很重要的点：

> 这次没有为了“位图”再引入复杂的新块大小，仍然让 data block 保持 512 字节。

这样做的好处是：

- 更接近真实磁盘读写单位
- big file 的 direct / indirect 行为更直观
- 位图和 inode 表已经足够提供这一轮要学的复杂度

---

## 4. superblock 现在多了什么

当前 superblock 已经从“更小的布局说明”长成了“96 字节的卷头信息”。

最关键的新字段有：

- `inode_bitmap_start_sector`
- `inode_bitmap_sector_count`
- `data_bitmap_start_sector`
- `data_bitmap_sector_count`
- `free_inode_count`
- `free_data_block_count`

你可以把它们理解成两类信息：

### 第一类：布局信息

也就是：

- inode 位图在哪里
- data 位图在哪里
- inode 表在哪里
- 数据区在哪里

这类字段的作用是：

> 让内核知道应该去磁盘的哪一段读对应结构。

### 第二类：摘要统计

也就是：

- 当前还剩多少空闲 inode
- 当前还剩多少空闲 data block

这类字段的作用是：

> 让内核和 shell 不用每次都扫全卷，就能先看到容量大概情况。

当然，
正式系统里这种摘要必须和真实位图保持一致，
所以当前挂载阶段还会再校验一次。

---

## 5. inode bitmap 和 data bitmap 是怎么用的

先说一个最容易混淆的点：

> “位图里写了占用” 和 “inode 真的引用了对应块” 不是同一件事。

位图只是声明：

```text
这个资源被占用了
```

而 inode / 目录 / 间接块这些结构才真正说明：

```text
这个资源是被谁占用的
```

所以当前挂载时会做两边对照。

### 5.1 inode bitmap

当前规则是：

- bit `0`：保留 inode 槽位
- bit `1..N`：真正文件系统对象

当前镜像里已经分配的 inode 有：

- `1` root dir
- `2` readme.txt
- `3` notes.txt
- `4` docs dir
- `5` guide.txt
- `6` hello.bin
- `7` hello.elf
- `8` big.txt

所以当前：

- 可分配 inode 总数：`31`
- 已用 inode：`8`
- 空闲 inode：`23`

### 5.2 data bitmap

data bitmap 记录的是：

> 数据区里的逻辑 block 哪些已经被占用。

当前被占用的块包括：

- 根目录块
- `readme.txt`
- `notes.txt`
- `docs` 目录块
- `guide.txt`
- `hello.bin`
- `hello.elf` 的两个数据块
- `big.txt` 的 single indirect block
- `big.txt` 的十个数据块

所以现在：

- 总 data block：`121`
- 已用 data block：`19`
- 空闲 data block：`102`

这正是 shell `disk` 命令里现在会打印出来的数字。

---

## 6. 为什么要做挂载期一致性校验

如果没有这一步，
位图就很容易沦为：

> 写在那里，但没人验证它是不是真的对。

这一轮挂载时做的事情可以概括成：

```text
扫描所有“被 inode bitmap 标记为已分配”的 inode
-> 检查 inode 本身是否合法
-> 收集它们实际占用的 direct block
-> 如果有 indirect block，也把 indirect block 自己和它指向的数据块一起记下来
-> 最后把收集结果和 data bitmap 对比
```

这一步主要能抓住几类典型错误：

### 6.1 位图说“空闲”，但 inode 其实在用

这种情况以后如果真做写支持，
分配器可能会把一个还在使用的块再次分配出去。

后果就是：

> 文件互相覆盖数据。

### 6.2 位图说“已占用”，但没有任何 inode 在引用

这种情况不会立刻损坏文件内容，
但会导致：

> 空间泄漏。

你明明还有空闲块，
却因为位图没清，内核以为它已经被占了。

### 6.3 superblock 里的空闲计数和真实情况对不上

比如 superblock 说：

```text
free_data_block_count = 102
```

但位图实际数出来不是这个数，
那就说明卷头摘要已经不可信。

正式系统里这种问题也必须尽早发现。

---

## 7. 这次为什么专门加了 mount error

这一轮还顺手做了一个很实用的工程化改进：

> 给 `OS64FS` 挂载过程补了明确的错误阶段码。

位置在：

- [kernel/fs/os64fs.hpp](/Users/zhuzhumingyang/githubProjects/aiProjects/os64/kernel/fs/os64fs.hpp:1)
- [kernel/fs/os64fs.cpp](/Users/zhuzhumingyang/githubProjects/aiProjects/os64/kernel/fs/os64fs.cpp:678)

这样一来，
如果文件系统挂载失败，
串口日志不再只是一个模糊的：

```text
filesystem bad
```

而是可以继续知道问题更接近：

- 块设备不对
- superblock 读失败
- 布局字段非法
- inode bitmap 读失败
- data bitmap 读失败
- inode table 读失败
- 根 inode 不合法
- 位图和真实占用不一致

对操作系统开发来说，
这种“早期失败可定位”非常重要。

因为 boot 阶段本来就很难调试，
如果你连失败在哪一步都看不见，
后面排错成本会非常高。

---

## 8. `big.txt` 在这一步为什么很关键

当前镜像里专门放了一份：

```text
/docs/big.txt
```

它不是为了“放内容”，
而是为了同时证明两件事：

1. `8` 个 direct block 真的打通了
2. single indirect block 真的打通了

当前 `big.txt` 的大小是：

```text
5120 bytes = 10 * 512-byte blocks
```

也就是：

- 前 `8` 块走 direct block
- 后 `2` 块走 indirect block

自动测试会专门读这几个位置：

- offset `0`
- offset `4095`
- offset `4096`
- offset `5119`

最后检查得到：

```text
os64fs_big_markers=AHIJ
```

这说明跨 direct / indirect 边界的读取已经正确。

所以 `big.txt` 其实是一个：

> 文件系统寻址能力的烟测夹具。

---

## 9. shell 里现在能看到什么变化

这一步之后，
`disk` 命令不再只会告诉你：

- 起始 LBA
- 扇区数
- 扇区大小
- 总字节数

它现在还会继续告诉你：

- `disk_fs_version=3`
- `disk_inode_bitmap_sectors=1`
- `disk_data_bitmap_sectors=1`
- `disk_inode_total=31`
- `disk_inode_used=8`
- `disk_inode_free=23`
- `disk_data_blocks=121`
- `disk_data_used=19`
- `disk_data_free=102`

这意味着 shell 现在已经不只是“看见块设备”，
而是开始能看见：

> 文件系统作为一个整体的资源占用情况。

这也是从“能读文件”走向“能管理存储”的重要一步。

---

## 10. 这一步还顺手修了一个真实构建坑

这次实现里还有一个很值得记住的小坑：

> `boot_volume.bin` 如果只 `truncate` 到目标大小，而不先清空旧文件，旧内容可能会残留。

这在以前的简单布局里不容易暴露，
但引入位图以后就很敏感了。

因为位图后面很多“看起来没写过”的字节，
如果残留的是上一次构建的数据，
挂载期一致性校验就会误以为：

```text
还有一些块被占用了
```

所以这一步在构建脚本里补了：

> 先把 `boot_volume.bin` 截到 `0`，再重新扩成目标大小。

这说明一个很实际的工程经验：

> 文件系统格式一旦开始依赖“未写区域必须为 0”，构建过程也必须保证镜像真的被清零。

---

## 11. 现在离“商业可用”还有多远

这一步确实比以前更像正式文件系统，
但它还远远不是完整商用品质。

当前仍然是：

- 只读
- 没有真正的 `create/write/unlink`
- 没有运行时块分配器
- 没有写回顺序控制
- 没有缓存层
- 没有崩溃恢复
- 没有 journal
- 没有多挂载点 / 多文件系统抽象

所以更准确地说，
这一步的意义是：

> 先把“正式文件系统最基础的元数据骨架”补齐。

这是一个很重要的分界线。

在它之前，
你只是“能读一些预先摆好的文件”。

在它之后，
你才第一次真正开始拥有：

> 可以继续往可写文件系统扩展的结构基础。

---

## 12. 下一步最合理接什么

如果继续沿这条存储线往前做，
最合理的顺序通常是：

1. 基于 `inode bitmap` 和 `data bitmap` 做真正的运行时分配器
2. 补第一批写操作：`create` / `mkdir` / `write` / `append`
3. 再补删除和回收：`unlink` / `rmdir`
4. 最后再考虑缓存、写回顺序和 crash consistency

也就是说：

> 这一步不是终点，它是在给“可写文件系统”铺地基。

---

## 13. 一句话总结

如果要把这次升级浓缩成一句话，就是：

> `OS64FS v3` 不是“又多存了几个字段”，而是第一次让这个教学文件系统开始显式管理“全卷资源分配状态”和“挂载期一致性”。

这也是为什么它比前一版更接近真正的存储系统。
