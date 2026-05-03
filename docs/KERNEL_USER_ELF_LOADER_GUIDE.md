# 从“第一版从文件系统加载用户程序”到“第一版 ELF 用户程序装载器”

先给你一个阅读定位提醒：

> 这篇重点是“内核怎样理解 ELF 文件并把它装进用户地址空间”。
> 当前仓库在这一步之后又继续往前走了一格：
> `/docs/hello.elf` 现在不只会被 `kernel_main` 手工跑一次，
> 还已经能被 scheduler 正式当成一条 user thread 启动。
> 所以下一篇建议接着看 `KERNEL_SCHEDULER_ELF_THREAD_GUIDE.md`。

先说这一步到底在干什么：

> 上一轮我们已经能把 `OS64FS` 里的 `hello.bin` 读出来，当成原始机器码直接跑。
> 这一轮继续往前，把用户程序升级成真正的 `ELF64` 可执行文件，再让内核第一次解析 `ELF header` 和 `program header`，按 `PT_LOAD` 把段映射进用户地址空间。

如果你是小白，可以先把它理解成一句话：

> 上一轮是“把一段裸机器码塞进内存里跑”，这一轮是“开始理解操作系统怎样装载一种正式程序格式”。

---

## 1. 为什么这一步重要

前一轮的 `hello.bin` 已经很重要了，
因为它第一次证明：

```text
文件系统里的字节
-> 读进物理页
-> 映射成用户代码页
-> iretq 进入 ring3
```

但是 `hello.bin` 还是“裸字节流”。

它的问题是：

- 内核不知道文件里哪一段是代码、哪一段是数据
- 内核也不知道真正入口地址在哪里
- 内核只能靠“我约定它从页首开始执行”这种外部假设

而真正的现代系统不会这么做。

现代系统要靠一种程序文件格式告诉内核：

1. 这是不是一个可执行文件
2. 它是给哪种 CPU 架构准备的
3. 程序入口地址在哪里
4. 哪些字节应该被装到哪个虚拟地址
5. 哪些内存区域需要读、写、执行权限

ELF 就是在做这件事。

所以这一步的意义是：

> 内核第一次不再靠“外部约定”，而是开始从程序文件自己的头部元数据里理解“该怎么装载它”。

---

## 2. 这一轮到底新增了什么

这一步主要新增了 5 件事：

1. 新增了真正的 ELF 用户程序源文件：`user/hello_elf.asm`
2. 新增了 ELF 链接脚本：`user/hello_elf.ld`
3. 构建脚本会产出 `build/hello.elf`，再写进 `OS64FS` 的 `/docs/hello.elf`
4. 内核新增了独立模块：`kernel/task/elf_loader.cpp`
5. 启动自测会真正加载 `/docs/hello.elf` 并进入 ring 3 执行

所以现在 `/docs` 目录已经从：

```text
guide.txt
hello.bin
```

继续变成：

```text
guide.txt
hello.bin
hello.elf
```

这很重要，因为你现在能同时对照两条装载路线：

- `hello.bin`：原始 flat binary 路线
- `hello.elf`：正式 ELF 路线

---

## 3. 为什么先做“最小 ELF loader”，而不是一步到位做完整 `exec`

很多人会想：

> 既然都上 ELF 了，为什么不直接做完整进程装载器？

原因还是那句：

> 先把一个变量讲明白，再加下一个变量。

如果你现在直接做完整 `exec`，
你会一次性撞上这些东西：

- ELF 头解析
- 多段映射
- BSS 零填充
- 参数栈
- 用户栈布局
- 环境变量
- 文件描述符继承
- 进程替换语义

这对当前阶段太重了。

所以这一轮故意只做“最小但正式”的版本：

- 支持 `ELF64`
- 支持 `x86_64`
- 支持 `ET_EXEC`
- 支持恰好 `1` 个 `PT_LOAD` 段

注意，
这一步虽然受限，
但它已经是真正的 ELF 装载，
不是假的。

---

## 4. `hello.elf` 是怎么生成出来的

这一步新增了两份文件：

- `user/hello_elf.asm`
- `user/hello_elf.ld`

### 4.1 `user/hello_elf.asm`

这份汇编程序做的事很少：

1. 切好用户态数据段寄存器
2. 打一条日志：

```text
user_elf_program=hello from elf
```

3. 用 `exit` 把成功标志 `0x40` 带回内核

也就是说，
这份程序的目标不是炫技，
而是专门当：

> “第一份真正从 ELF 文件装载起来的用户程序”

### 4.2 `user/hello_elf.ld`

这份 linker script 的作用，是把 ELF 组织成最适合当前教学 loader 理解的形状。

它最关键的设计有两个。

#### 第一：只做 1 个 `PT_LOAD`

这样当前内核 loader 不用马上处理多段映射。

#### 第二：让 `ELF header` 和 `program header` 也落进这个可加载段

也就是：

```text
文件偏移 0
-> 映射到用户虚拟地址 0x400000
```

这样：

- ELF 文件开头的头部信息也会一起落进内存
- 真正入口 `_start` 会落在这些头部之后
- 所以内核看到的入口地址不是 `0x400000`
- 而是：

```text
0x400080
```

这就是你在日志里看到：

```text
user_elf_program_entry=0x0000000000400080
```

的原因。

---

## 5. 为什么 `hello.elf` 还能放进当前 OS64FS

你可能会担心：

> ELF 不是会比裸机器码大很多吗？

确实更大。

当前 `hello.bin` 只有：

```text
87 bytes
```

而这一步的 `hello.elf` 大约是：

```text
424 bytes
```

但它仍然能放进当前文件系统，
因为：

1. `hello.elf` 被 `objcopy --strip-all` 处理过，去掉了没必要的调试和符号信息
2. 当前 inode 有 4 个 direct block
3. 每个 block 是 `128` 字节

所以单文件最大还能装：

```text
4 * 128 = 512 bytes
```

`424` 还在范围内。

为了同时保留：

- `guide.txt`
- `hello.bin`
- `hello.elf`

这一步把 boot volume 从 `4` 个扇区扩成了 `5` 个扇区，
数据区从 `2` 个扇区扩成了 `3` 个扇区。

所以现在串口里你会看到：

```text
boot_volume_sector_count=5
block_device_total_bytes=2560
os64fs_inode_count=8
os64fs_docs_entries=3
ls[2]=file hello.elf size=424
```

---

## 6. ELF 文件里，内核这一轮到底读了什么

这一步里，
内核并没有“理解整个 ELF 世界”，
它只理解了最核心的那部分。

### 6.1 先读 ELF header

ELF header 会告诉内核：

- 这是不是 ELF 文件
- 是不是 64 位
- 是不是小端
- 是不是 `x86_64`
- 是不是 `ET_EXEC`
- 程序入口地址是多少
- program header 表从文件哪个偏移开始
- program header 一共有几项

所以这一步其实是：

> 先让程序文件自己描述“我是什么、从哪开始、去哪里看下一层信息”。

### 6.2 再读 program header

program header 才是“怎么装”的关键。

当前我们只关心 `PT_LOAD`。

它告诉内核：

- 从文件哪个偏移开始读
- 读多少字节
- 这些字节要放到哪个虚拟地址
- 这段在内存里最终占多少字节
- 权限标志是什么

你可以把它理解成一句话：

> ELF header 解决“这是什么文件”，program header 解决“怎么把它放进内存”。

---

## 7. 这一轮的内核 loader 是怎么工作的

真正干活的模块是：

```text
kernel/task/elf_loader.cpp
```

它现在的主函数是：

```text
load_elf_user_program(...)
```

这条主链可以拆成 8 步。

### 第一步：打开 `/docs/hello.elf`

这里不是直接去读 inode，
而是继续复用已经有的文件层：

- `file_open`
- `file_handle_stat`
- `file_read`

这样做的原因是：

> ELF loader 是“在文件层之上解释程序格式”，不应该重新绕过现有文件抽象。

### 第二步：把整个 ELF 文件读进一个 staging page

这一轮故意先要求 ELF 文件大小：

```text
<= 4096 bytes
```

所以内核可以先分配 1 张物理页，
把整个 ELF 文件都读进去。

为什么要这样做？

因为当前阶段最重要的是把：

```text
file -> header -> segment -> user page
```

讲清楚，
而不是先做复杂的流式读取。

### 第三步：校验 ELF header

这一步会检查：

- magic 是否是 `0x7F 'E' 'L' 'F'`
- class 是否是 `ELF64`
- data 是否是 little-endian
- machine 是否是 `x86_64`
- type 是否是 `ET_EXEC`

如果这些不对，
内核就直接拒绝装载。

这非常重要，
因为内核不能“猜”一个文件是不是程序。

### 第四步：找到 `PT_LOAD`

这一轮 loader 会遍历 program header 表，
统计 `PT_LOAD` 段数量。

当前限制是：

> 必须恰好只有 1 个 `PT_LOAD`

为什么要这样限制？

因为这样最容易讲清楚“一个段是怎样被映射的”。

等这一步理解透彻，
再扩到多个 `PT_LOAD` 会自然很多。

### 第五步：验证段范围是否合法

这里会检查：

- 文件偏移有没有越界
- `file_size <= memory_size`
- 段虚拟地址是否落在用户地址空间窗口里
- 需要映射的页数有没有超出当前 loader 限制

这些检查的本质都是一句话：

> 内核不能盲信 ELF 文件，必须先确认“它要求装到哪里、会不会越界、会不会把自己带沟里去”。

### 第六步：分配并映射用户页

内核会根据 `PT_LOAD` 的虚拟地址范围，
算出一共要覆盖多少个用户页。

然后一页一页地：

1. `alloc_page`
2. 清零
3. `address_space_map_user_page`

如果 `PF_W` 打开，
就把页映射成可写；
否则保持只读代码页风格。

### 第七步：把段内容拷进这些页

这一步是当前 loader 最核心的动作。

注意：

- 文件偏移
- 段虚拟地址
- 页边界

这 3 个东西不是天然重合的。

所以 loader 这一步做的是：

1. 遍历段里的每一个文件字节
2. 算它最终应该落到哪个虚拟地址
3. 再反算它属于哪一张物理页、页内偏移是多少
4. 最后把字节写进去

所以你要真正理解的是：

> ELF 装载的本质不是“复制一个文件”，而是“按程序头描述，把文件字节重新摆到正确的内存布局里”。

### 第八步：把 entry point 交给 `user_mode_enter()`

前一轮 flat binary 里，
入口地址就是：

```text
0x400000
```

这一轮不再这么写死，
而是从 ELF header 的 `entry` 字段里读出：

```text
0x400080
```

然后把它填进：

```text
session.user_instruction_pointer
```

最后再 `iretq` 进入 ring 3。

这就是这一步最大的升级：

> 内核第一次不是“我约定入口在哪”，而是“我从程序文件里读出入口在哪”。

---

## 8. 这一步和上一轮 flat binary 的本质区别

你可以把这两轮对比着记。

### 上一轮 `hello.bin`

- 文件内容就是机器码
- 入口默认是代码页开头
- 内核只需要把整段字节拷进去

### 这一轮 `hello.elf`

- 文件开头先是 ELF 头和 program header
- 真正入口不一定在文件开头
- 内核要先解析头，再按段映射，再跳到 `entry`

所以一句话总结差别：

> flat binary 是“我知道这堆字节该怎么跑”；ELF 是“文件自己告诉我它该怎么跑”。

---

## 9. 这一轮日志里最关键的几行是什么意思

现在如果一切正常，
你会看到这些关键日志：

```text
user_elf_program_path=/docs/hello.elf
user_elf_program_inode=7
user_elf_program_entry=0x0000000000400080
user_elf_program_file_size=424
user_elf_program_segment_count=1
user_elf_program_segment_vaddr=0x0000000000400000
user_elf_program_segment_offset=0x0000000000000000
user_elf_program_segment_filesz=215
user_elf_program_segment_memsz=215
user_elf_program_segment_flags=0x0000000000000005
user_elf_program_page_count=1
user_elf_program=hello from elf
user_elf_program_return_flags=0x0000000000000040
user_elf_program ok
```

这些分别说明：

- 内核确实打开的是 `/docs/hello.elf`
- ELF 文件确实在文件系统里
- 真正入口不是页首，而是 `0x400080`
- 这个 ELF 里只有 1 个 `PT_LOAD`
- 这 1 个段被映射到了 `0x400000`
- 权限标志 `0x5` 就是 `R + X`
- 程序确实在 ring 3 运行并正常返回

---

## 10. 当前 ELF loader 还故意没做什么

这一步虽然已经是真正的 ELF 装载，
但它还故意没有做这些事：

- 多个 `PT_LOAD` 段
- 独立 `.data` / `.bss`
- 更大的多页文件
- `ET_DYN`
- 动态链接
- 参数栈和环境变量
- 完整 `exec` 语义

这不是缺点，
而是这一步的教学边界。

你现在最该先吃透的是：

> 内核怎样从一个 ELF 文件里读出 entry 和 segment，再把它变成 ring 3 里真正执行的用户程序。

---

## 11. 下一步最合理做什么

完成这一步以后，
下一步最合理的方向其实就清楚了：

1. 扩成多个 `PT_LOAD` 段
2. 把 `.bss` / 可写数据段做得更正式
3. 再把这条装载链接进真正的 process/thread 创建路径

也就是说，
下一步不应该再回去强化 flat binary，
而是继续把：

```text
ELF 文件
-> 更正式的进程装载
-> 更正式的用户进程启动
```

这一条现代操作系统真正会走的路继续做深。
