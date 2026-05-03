# 从“第一次真正进入用户态”到“第一版从文件系统加载用户程序”

这一步的目标很明确：

> 不再只运行“编译进内核镜像里的用户态字节数组”，而是第一次真的把一个用户程序文件放进 `OS64FS`，再由内核把它读出来、映射进用户地址空间、最后进入 ring 3 执行。

如果你是小白，可以先把这一轮理解成一句话：

> 以前用户程序像“写死在老师讲义里的例题”，现在它第一次变成了“磁盘里的一个独立文件”。

---

## 1. 这一轮到底新增了什么

这一步主要新增了 4 件事：

1. 新增了一个真正放在文件系统里的用户程序：`user/hello.asm`
2. 构建脚本会把它汇编成原始 flat binary，然后写进 `OS64FS` 的 `/docs/hello.bin`
3. 内核新增了一个“小用户程序装载器”，会把 `/docs/hello.bin` 读进 1 张物理页，再映射成用户代码页
4. 启动自测会真的进入 ring 3 跑这份文件加载程序，并验证它通过 `int 0x80` 正常返回

所以这一轮虽然代码量不算最大，但意义很大：

- 你第一次让“文件系统”和“用户态执行”真正连起来了
- 后面要做 ELF、进程加载、用户程序启动器，本质上都要建立在这条链上

---

## 2. 为什么先不做 ELF，而是先做一个很小的 flat binary

很多人学到这里会问：

> 为什么不直接实现 ELF 装载器？

原因不是“不能做”，而是“现在做会把 3 个问题搅在一起”。

如果你现在直接上 ELF，你要同时处理：

1. 文件系统里怎么存这个程序
2. 内核怎么解析 ELF 头和 program header
3. 内核怎么按段把内容映射到目标虚拟地址

这对当前项目阶段来说太杂了。

所以这一轮先故意把问题压缩成最小版本：

- 用户程序就是一段原始机器码
- 整个文件一次性读到 1 张页里
- 入口地址就固定是那张页的开头

这样你先学会最核心的事：

> “一个文件里的字节，怎样变成 CPU 真的能执行的用户态代码页。”

等这一步完全吃透后，再上 ELF 会顺很多。

---

## 3. 为什么不把现有大用户程序直接放进文件系统

你现在项目里其实已经有一个更复杂的用户程序，
就是 `kernel/task/context_switch.asm` 里的 `user_mode_yield_program_start .. end`。

它已经能做这些事：

- `write`
- `getcwd`
- `open/read/close`
- `yield`
- timer 抢占恢复
- `read(0)` 阻塞再唤醒

但这段程序当前大约 `920` 字节。

而现有 `OS64FS` v1 每个 inode 只有 4 个 direct block，
而且现在为了保持布局简单，每个数据块只有 `128` 字节。

这意味着：

- 当前单文件最小稳定布局里，想继续保持“每个普通小文件 1 个块”的教学风格
- 那么这次最合理的做法就是先放一个很小的程序

所以这一步没有把“大用户线程测试程序”直接搬进文件系统，
而是专门新增了一个极小程序 `hello.bin`。

这样做的好处是：

- 先把链路打通
- 不会马上逼着你重构文件系统格式
- 后面再做 ELF 或大文件用户程序时，问题边界更清楚

---

## 4. `OS64FS` 里这次怎么摆

这一步之前，文件系统大致是这样：

```text
/
├── readme.txt
├── notes.txt
└── docs/
    └── guide.txt
```

现在改成：

```text
/
├── readme.txt
├── notes.txt
└── docs/
    ├── guide.txt
    └── hello.bin
```

这一轮刻意没有增加根目录项数量，
而是把 `hello.bin` 放到现有 `/docs` 目录下面。

原因很简单：

- 根目录现在保持 3 个目录项就够了
- `/docs` 从 1 项变成 2 项，改动最小
- 不需要马上扩大更多文件系统结构

当前布局大致是：

```text
block 0 -> 根目录
block 1 -> readme.txt
block 2 -> notes.txt
block 3 -> docs 目录
block 4 -> guide.txt 前半
block 5 -> guide.txt 后半
block 6 -> hello.bin
block 7 -> 预留
```

对应 inode 现在是：

- inode 1: `/`
- inode 2: `readme.txt`
- inode 3: `notes.txt`
- inode 4: `docs`
- inode 5: `guide.txt`
- inode 6: `hello.bin`

所以你现在在串口日志里会看到：

- `os64fs_inode_count=7`
- `os64fs_docs_entries=2`
- `os64fs_hello_inode=6`

---

## 5. 构建脚本这次做了什么

这一步改的是 `scripts/build-stage1-image.sh`。

它新增了两件关键工作。

### 5.1 先把 `user/hello.asm` 汇编成原始二进制

现在脚本除了汇编：

- `stage1.asm`
- `stage2.asm`
- `kernel/boot/entry64.asm`
- `kernel/task/context_switch.asm`

还会额外做一件事：

```text
nasm -f bin user/hello.asm -> build/hello.bin
```

注意这里用的是：

```text
-f bin
```

不是 `elf64`。

原因就是：

> 我们现在要得到的是“纯字节流文件”，不是带 ELF 头的目标文件。

### 5.2 再把 `build/hello.bin` 写进 boot volume 的数据区

构建脚本随后会：

1. 增加 inode 数量到 `7`
2. 把 `/docs` 目录项数量从 `1` 改成 `2`
3. 给 inode `6` 写入 `hello.bin` 的大小和 direct block
4. 把目录项 `hello.bin` 写到 `/docs` 目录
5. 最后把 `build/hello.bin` 用 `dd` 写进 `block 6`

这里还专门加了一个大小检查：

- `hello.bin` 不能为空
- `hello.bin` 不能超过 `128` 字节

原因是当前这一轮的布局就是：

> 一个小用户程序 = 一个 inode + 一个数据块

这样最利于学习和调试。

---

## 6. `user/hello.asm` 这份程序到底在干什么

这份程序非常小，
它只做 3 件事：

1. 把 `DS/ES/FS/GS` 切到用户态数据段
2. 发一次 `write(1, "user_file_program=hello from fs\n")`
3. 发一次 `exit()`，并把成功标志 `0x20` 带回内核

你可以把它理解成：

> “这是第一份从文件系统装载出来的最小用户态验收程序。”

为什么它不做 `open/read/getcwd` 那些更复杂的事？

因为这些能力已经由前面的 `user_mode_smoke_program` 验证过了。

这一步的新重点不是“用户程序能做多少事”，
而是：

> 文件里的字节，能不能真的跑起来。

所以这份程序越小越好。

---

## 7. 内核怎么把 `/docs/hello.bin` 变成用户代码页

这一轮内核里新增的关键 helper 是：

```text
load_user_program_file(...)
```

它做的事情可以拆成 6 步。

### 第一步：按路径打开文件

先用：

- `file_open(filesystem, "/docs/hello.bin", &file_handle)`

原因是当前教学项目已经有 `FileHandle` 层了。

所以这一步不应该再直接绕回 inode 读块逻辑，
而是要复用现有文件接口。

### 第二步：拿文件元数据

再用：

- `file_handle_stat(...)`

拿到：

- inode 编号
- 文件大小

为什么一定要先拿大小？

因为内核要先知道：

> 这份程序能不能塞进当前“单页用户代码”模型里。

这一步当前只允许：

- `size > 0`
- `size <= 4096`

也就是：

> 先支持“一页以内的用户程序”。

### 第三步：申请一张物理页

然后内核调用页分配器申请 1 张物理页。

这张页就是稍后要变成用户代码页的那张页。

### 第四步：把文件内容读进这张物理页

这里做的是：

1. 先把整页清零
2. 再用 `file_read(...)` 把程序内容读进去

注意：

> 这里还没有进用户态，CPU 还在内核态里操作一块普通物理页。

此时它还只是“一块装着程序字节的内存”。

### 第五步：把这张物理页映射到用户虚拟地址

现在调用：

- `address_space_map_user_page(...)`

把它映射到：

- `0x0000000000400000`

也就是当前项目约定的用户代码页起始地址。

到这一步，这块字节才真正具备了：

> “以后 CPU 切进 ring 3 后，可以从这个虚拟地址取指执行。”

### 第六步：准备用户栈并 `iretq` 进入 ring 3

最后和前一轮 `run_user_mode_smoke_test()` 一样：

1. 再分配 1 张用户栈页
2. 准备 `UserModeLaunchContext`
3. 调 `user_mode_enter(&session)`

汇编入口会：

1. 保存当前内核恢复栈
2. 切换到用户地址空间对应的 `CR3`
3. 伪造 `iretq` 需要的返回帧
4. 真正落进 ring 3

所以这一轮你要真正记住的不是函数名，
而是这个顺序：

```text
file bytes
-> read into physical page
-> map into user virtual address
-> prepare user stack
-> iretq
-> ring3 executes that file
```

---

## 8. 为什么返回值里还要带 `CS + 成功标志`

这一步里，`hello.bin` 退出时并不只是简单返回 `0`。

它会把返回值拼成两部分：

1. 低 16 位：用户态看到的 `CS`
2. 高位：这份文件程序自己的成功标志 `0x20`

这样内核回来后能同时验证两件事：

1. 这次程序真的运行在 ring 3
2. 这次返回的确实是“文件加载版用户程序”的成功路径

所以串口里现在会看到：

- `user_file_program_return_cs=0x0000000000000043`
- `user_file_program_return_cpl=3`
- `user_file_program_return_flags=0x0000000000000020`

这 3 行很关键。

它们说明的不是“打印了点字”，
而是：

> 这份文件里的机器码，真的在用户态跑完一轮，又用受控方式回到了内核。

---

## 9. 这一步为什么重要

这一步完成以后，
你的项目状态已经从：

> “内核能自己手工塞一段用户字节进去运行”

推进到了：

> “内核已经能从自己的文件系统里拿到一份用户程序并执行”

这会直接为后面几步铺路：

1. ELF 装载器
2. `exec` 风格进程装载
3. 用户程序镜像管理
4. 多页代码段 / 数据段映射
5. 更正式的用户程序入口约定

所以这一轮虽然还是个小程序，
但它已经是“真正程序装载”的第一块台阶。

---

## 10. 这一步怎么测试

现在可以直接跑：

```bash
make test-stage1
make test-page-fault
make test-invalid-opcode
```

这 3 个测试当前都已经覆盖了这一步。

如果成功，你会在串口日志里看到这些关键标记：

```text
os64fs_inode_count=7
os64fs_docs_entries=2
os64fs_hello_inode=6
user_file_program_path=/docs/hello.bin
user_file_program=hello from fs
user_file_program_return_flags=0x0000000000000020
user_file_program ok
ls_path=docs
ls_resolved_path=/docs
ls_entry_count=2
ls[1]=file hello.bin size=87
```

这些日志组合起来分别证明：

- 文件系统里确实多了 `hello.bin`
- shell 确实能看见它
- 内核确实能打开它
- 用户态确实跑过它
- 程序也确实从 ring 3 正常返回了

---

## 11. 当前这套装载器还有哪些限制

这一步故意保留了很多限制，
因为当前目标是“先把最短主链打通”。

现在它还不支持：

- ELF 解析
- 多段装载
- 多页代码
- 用户数据段和 BSS 分离
- 参数传递
- 环境变量
- 动态链接

但这不是缺点，
而是这一步的边界。

你现在应该先把这一步彻底看懂：

> 一个普通文件，怎样通过页分配器、页表、`CR3` 和 `iretq` 变成一个真正执行的用户程序。

这件事一旦懂了，
后面的 ELF / `exec` / 用户进程装载就有了非常稳的地基。
