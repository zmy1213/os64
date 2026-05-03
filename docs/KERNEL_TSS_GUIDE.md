# 从“shell 真正跑进调度器”到第一版 `TSS`

这一轮补的不是新命令，
也不是马上做 ring 3 用户态，
而是先把以后一定会用到、但前面一直还缺的一层补上：

> 第一版 `x86_64 TSS`（Task State Segment）

一句话先说结论：

```text
以前：内核已经有 IDT / trap / scheduler / int 0x80
现在：再补上 kernel-owned GDT + 64 位 TSS + RSP0 + double-fault IST1 + ltr
```

它的意义不是“现在立刻做硬件任务切换”，
而是：

> 为以后真正进入 ring 3、从用户态切回内核态、以及在坏栈场景下安全接异常，先把 CPU 需要的基础设施准备好。

---

## 1. 这一步到底在解决什么

前一轮你已经有了很多看起来很像现代内核的东西：

- 64 位 long mode
- 页表
- IDT
- page fault / invalid opcode 异常入口
- `int 0x80`
- scheduler
- shell 真正跑进调度器

但这里还差一个很关键的问题：

```text
如果以后真的有 ring 3 用户线程，
CPU 从用户态进入内核态时，
到底该切到哪一根内核栈？
```

还有另一个问题：

```text
如果当前栈本身已经坏了，
像 double fault 这种更严重的异常，
CPU 还能不能切到另一根“应急栈”继续处理？
```

这两个问题都不是普通 C++ 代码自己说了算，
而是 CPU 规定要通过：

```text
TSS
```

来告诉它。

所以这一步真正补的是：

```text
“内核如何把自己的栈切换规则交给 CPU”
```

---

## 2. 先把一个误区讲清楚：64 位 TSS 不是“老式任务切换”

很多教程一提 `TSS`，
会连着讲“硬件任务切换”“task gate”这些老内容，
容易把小白讲晕。

在这个项目里，
你现在可以先只记住：

```text
64 位 TSS 最重要的用途
不是让 CPU 自动切任务
而是提供：
1. ring 3 -> ring 0 时要切到哪根内核栈
2. 某些严重异常要不要切到专用 IST 栈
```

所以这里的 `TSS` 先不是“调度器”，
而是：

> CPU 进入内核时要参考的一张“栈切换配置表”。

---

## 3. 为什么这一步还要重新建一份 `GDT`

很多人看到这里会问：

```text
不是 stage2 里已经建过 GDT 了吗？
为什么进了 kernel 还要再来一次？
```

原因有两个。

### 3.1 stage2 的 GDT 只是“把机器带进 long mode”

之前 stage2 那份 GDT 的主要任务是：

- 从实模式进保护模式
- 从保护模式进 long mode
- 让 CPU 能跳进 64 位内核入口

也就是说它首先是：

```text
启动阶段的 GDT
```

不是：

```text
内核长期持有、长期扩展的 GDT
```

### 3.2 现在需要往 GDT 里加一个 64 位 TSS 描述符

CPU 不会凭空知道你的 TSS 在哪。

你必须把：

- TSS 的基地址
- TSS 的大小
- TSS 的类型

编码进一个描述符里，
再把这个描述符放进 GDT。

而且这里要注意：

```text
64 位 TSS 描述符不是 8 字节
而是 16 字节
```

也就是：

```text
它会占两个 GDT 槽位
```

所以这一轮最合理的做法就是：

> 内核自己安装一份长期可控的 GDT，然后把 TSS 描述符接进去。

---

## 4. 这一轮 TSS 里真正用到哪些字段

这一轮没有把 TSS 的所有可能字段都用满，
只先用最关键的 3 个：

### 4.1 `rsp0`

这是最重要的字段。

它表示：

```text
以后如果 CPU 从 ring 3 进入 ring 0，
应该先把 RSP 切成哪一个内核栈顶
```

现在虽然我们还没真的进入 ring 3，
但先把它准备好，
后面做用户态时就不会突然发现：

```text
“系统调用入口有了
但是内核栈切换规则根本没告诉 CPU”
```

### 4.2 `ist1`

`IST` 是 Interrupt Stack Table。

你可以先把它理解成：

```text
给某些特别危险的异常单独准备的应急栈
```

这一轮只先用：

```text
IST1
```

并把它挂给：

```text
double fault（向量 8）
```

原因很现实：

如果系统已经栈坏了，
而你还强行沿用当前栈去处理更严重的异常，
很容易直接一路崩成 triple fault。

所以 double fault 最适合先走独立应急栈。

### 4.3 `io_map_base`

TSS 末尾还可以挂 I/O permission bitmap，
用来控制某些任务能不能直接碰 I/O 端口。

但这一轮我们还没实现这套权限图，
所以做法是：

```text
把 io_map_base 设到 TSS 末尾
```

这等于告诉 CPU：

```text
现在先不启用额外 I/O bitmap
```

---

## 5. 这一轮具体改了哪些文件

主要是这几处：

- `kernel/boot/segments.hpp`
- `kernel/interrupts/interrupts.hpp`
- `kernel/interrupts/interrupts.cpp`
- `kernel/core/kernel_main.cpp`
- `scripts/test-stage1.sh`
- `scripts/test-invalid-opcode.sh`
- `scripts/test-page-fault.sh`

一句话分工：

- `segments.hpp`
  把代码段、数据段、TSS 选择子以及 `IST1` 这些常量单独收口。
- `interrupts.cpp/.hpp`
  负责真正创建 TSS、构建内核自己的 GDT、`lgdt`、`ltr`，以及给 double fault 门挂 `IST1`。
- `kernel_main.cpp`
  负责在正常启动链最前面补一个 TSS 烟测，并把关键值打到串口日志。
- `scripts/test-*.sh`
  负责把 TSS 这条新路径也纳入自动回归。

---

## 6. 启动时现在到底多做了什么

你可以把现在的主线理解成：

```text
kernel_main()
  -> run_tss_smoke_test()
  -> initialize_idt()
  -> 后面的页表/堆/文件系统/调度器/ shell 测试
```

这里第一步 `run_tss_smoke_test()` 里面又做了几件关键事。

### 6.1 先清空并填写 `g_kernel_tss`

这里主要做的是：

- `rsp0 = 内核态栈顶`
- `ist1 = double fault 应急栈顶`
- `io_map_base = sizeof(TSS)`

也就是先把最重要的 3 个字段准备好。

### 6.2 再构建 `g_kernel_gdt`

前 5 个描述符仍然和 stage2 保持兼容：

- null
- 32 位代码段
- 32 位数据段
- 64 位代码段
- 64 位数据段

这样做的好处是：

```text
前面已经写死在启动链和 IDT 里的选择子值不用整体重写
```

然后再往后补：

```text
64 位 TSS 描述符
```

它占两个槽位，
所以：

```text
TSS selector = 0x28
```

### 6.3 `lgdt` 把 CPU 指向内核自己的 GDT

这一步的意思很直接：

```text
以后这台 CPU 不再继续依赖 stage2 那份 GDT
而是开始使用 kernel 自己维护的 GDT
```

### 6.4 用 `lretq` 刷新 `CS`

很多人会困惑：

```text
为什么 lgdt 之后还不够？
```

因为 `CS` 这种段寄存器在 CPU 里有“隐藏缓存”，
单纯换 `GDTR` 不会自动让当前 `CS` 立刻重读新描述符。

所以这里做法是：

```text
push 代码段选择子
push 返回 RIP
lretq
```

你可以把它理解成：

> 让 CPU 真正执行一次“远返回”，从而把新的 64 位代码段重新装进 `CS`。

### 6.5 再重装 `DS/ES/SS/FS/GS`

同样道理，
数据段寄存器也最好一起刷新，
这样当前执行环境就完全切到新 GDT 了。

### 6.6 最后用 `ltr` 装载 TSS

这一步最关键。

`ltr` 的意思是：

```text
Load Task Register
```

也就是把：

```text
TSS selector
```

真正装进 CPU 的：

```text
task register
```

只有做完这一步，
CPU 以后在需要用 `TSS` 的场景下，
才知道该去哪里找它。

---

## 7. 为什么 double fault 要先挂 `IST1`

这一轮虽然还没做“所有异常都切专用栈”，
但 double fault 很值得先单独处理。

原因是：

### 7.1 它通常意味着“前一次异常处理已经出问题了”

比如：

- 栈已经坏了
- page fault 处理又再次 fault
- 某个关键异常入口继续踩坏状态

这时候如果你还继续沿用“当前正在坏掉的那根栈”，
CPU 很可能直接走向：

```text
double fault
-> 无法可靠处理
-> triple fault
-> 机器重启 / QEMU 直接死
```

### 7.2 IST 就是给这种场景准备的

所以现在做的不是“把异常处理做花”，
而是先让最危险的一条路径更稳一些：

> 一旦 double fault 到来，CPU 至少还有机会切到另一根干净栈继续处理。

---

## 8. 这一步做完以后，系统“已经有了什么”和“还没有什么”

### 已经有了什么

- 内核自己维护的 GDT
- 64 位 TSS 描述符
- `RSP0`
- double fault `IST1`
- `ltr` 装载 task register
- TSS 串口烟测

### 还没有什么

- 真正的 ring 3 用户线程
- `iretq` 返回用户态
- 每进程独立用户地址空间
- `syscall/sysret`
- `swapgs`
- 更完整的异常栈策略

所以你现在可以把它理解成：

```text
“用户态切换的关键前置条件已经补了一层”
```

但还不是：

```text
“系统已经能跑真正的用户态程序”
```

---

## 9. 现在启动日志里会多看到什么

这一轮正常启动和异常测试里，
都会多看到这些串口日志：

- `tss_rsp0=0x...`
- `tss_ist1=0x...`
- `tss_task_register=0x0000000000000028`
- `tss_io_map_base=104`
- `tss ok`

你可以这样理解它们：

- `tss_rsp0`
  说明 ring 0 栈顶已经真的写进 TSS。
- `tss_ist1`
  说明 double fault 应急栈已经准备好了。
- `tss_task_register`
  说明 `ltr` 后 CPU 里的 task register 已经真的指向 `0x28` 这个 TSS 选择子。
- `tss_io_map_base=104`
  说明当前 I/O bitmap 被明确设成“先不启用”。
- `tss ok`
  说明这一轮最小 TSS 验证通过。

---

## 10. 怎么测试这一轮

先正常启动回归：

```bash
make test-stage1
```

再跑非法指令异常：

```bash
make test-invalid-opcode
```

最后跑页错误异常：

```bash
make test-page-fault
```

注意还是按这个顺序一条一条跑，
不要并行。

原因很简单：

```text
它们共用 build/disk.img
```

并行跑的话，
一个测试可能刚把“正常镜像”写完，
另一个测试又把“page fault 专用镜像”覆盖掉了，
最后串口日志就会互相污染。

---

## 11. 如果你现在是小白，最该抓住哪句话

这一轮最核心的理解不是“记住 TSS 所有字段”，
而是这句：

> `TSS` 不是给你当前 shell 用来“切线程”的，而是给 CPU 以后从用户态回到内核态、以及在坏栈异常里安全切栈用的。

你只要先把这句话抓牢，
后面再看：

- ring 3
- 用户栈
- `iretq`
- `syscall/sysret`
- 更正式的异常入口

就不会把“调度器在做的事”和“TSS 在做的事”混成一团。

---

## 12. 下一步最合理做什么

上面当时建议的第一件事：

- 给 `ProcessControlBlock` 挂每进程地址空间骨架

现在已经接到了下一篇：

- [从第一版 `TSS` 到“每进程地址空间骨架”](./KERNEL_ADDRESS_SPACE_GUIDE.md)

所以如果你准备继续往用户态推进，
最顺的阅读路径就是：

```text
TSS
-> AddressSpace
-> 用户入口栈帧
-> iretq 进入 ring 3
-> ring 3 打 int 0x80 回来
```
