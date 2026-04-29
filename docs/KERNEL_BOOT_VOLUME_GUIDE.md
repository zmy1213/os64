# 从对象分配到最小扇区读取 + 启动卷

这份文档专门讲这一轮为什么继续做：

- stage2 预读 boot volume
- BootInfo 继续扩展
- 内核里的最小 sector read 接口
- 为什么这一步还不是真正的磁盘驱动

---

## 1. 为什么这一步不是“直接写文件系统”

上一轮你已经有了：

- 页分配器
- 页表
- 可 `free` 的堆
- `kmalloc` / `knew`
- 能交互的 shell

这说明：

> 内核内部“拿内存、建对象、做观察命令”这几条基础链已经站住了。

这时候如果你想继续往“文件系统”走，
你首先缺的不是目录树概念，
而是一个更基础的问题：

> 内核到底怎么先读到“扇区数据”？

没有这一步，
后面的文件系统代码就没有真实输入源。

---

## 2. 为什么不能在 64 位内核里直接继续调 BIOS 读盘

你前面的 `stage1` / `stage2` 读盘，
靠的是 BIOS 的：

```text
int 13h
```

但 BIOS 中断服务本质上是：

- 16 位实模式接口
- 依赖 BIOS 自己的中断向量和环境

现在你已经在：

- 保护模式
- long mode
- 64 位 C++ 内核

里继续往前走了。

所以这里最关键的现实是：

> 进了 64 位内核以后，不能再理直气壮地继续 `int 13h` 读盘。

这也是为什么“bootloader 能读盘”不等于“kernel 已经会读盘”。

---

## 3. 为什么这一轮不直接硬写软驱控制器驱动

因为你现在的 QEMU 启动介质还是：

```text
if=floppy
```

也就是一张传统软盘镜像。

如果你要在内核里真的直接驱动它，
下一步就不是“写几行读扇区代码”了，
而是会立刻碰到：

- 软驱控制器命令协议
- DMA
- IRQ
- 马达启动/等待
- 控制器状态机

这一步非常硬，
而且它会把你从“先把块读取模型打通”直接拖进“老硬件控制器细节泥潭”。

所以这一轮的策略是故意分两步：

1. 先把“扇区读取模型”打通
2. 以后再决定是写真正的 ATA/FDC 驱动，还是换更现代的启动/块设备路径

---

## 4. 这一轮到底做了什么

这一轮的核心思路可以概括成：

```text
磁盘镜像里的 boot volume
-> stage2 还在实模式时先把它读进内存
-> BootInfo 告诉 64 位内核这段数据在哪里
-> 内核把它包装成 sector read 接口
```

也就是说，
这一步不是让内核自己去碰控制器，
而是先让内核学会：

> “给我一个起始地址和扇区数，我怎么把它当成块设备数据来读。”

这已经足够支撑你下一步开始写：

- 启动卷头解析
- 目录项解析
- 最小文件读取

---

## 5. BootInfo 为什么要继续扩展

以前 `BootInfo` 只传：

- E820 条数
- E820 每项大小
- E820 指针

这一轮又多传了：

- `boot_volume_ptr`
- `boot_volume_start_lba`
- `boot_volume_sector_count`
- `boot_volume_sector_size`

原因很直接：

> 内核如果连“这段预读数据在哪”都不知道，就谈不上在 64 位里继续读扇区。

你可以把它理解成：

- stage2 是搬运工
- BootInfo 是交接单
- kernel 是真正开始消费这批块数据的人

---

## 6. boot volume 是什么

这一步的 `boot volume` 不是成熟文件系统，
而是一小段我们自己定义的卷数据。

它现在的结构非常小：

### 第 0 扇区

放一个最小卷头：

- 签名 `OS64VOL1`
- 版本号
- 总扇区数
- 扇区大小
- 卷名
- 两段演示文本分别在哪个扇区

### 第 1 扇区

放一段 README 风格的文本：

```text
boot volume sector 1: hello from os64
```

### 第 2 扇区

放第二段文本：

```text
boot volume sector 2: next step is filesystem
```

它的意义不是“这就是最终文件系统”，
而是先提供一份：

> 真的在磁盘镜像里、真的被 stage2 读进来、真的被 kernel 按扇区再次读取的数据

---

## 7. 内核现在多了什么能力

这一轮新增的 `storage/boot_volume.*` 做了三件事：

### 第一件：初始化启动卷

先检查：

- BootInfo 里的指针是不是有效
- 扇区大小是不是 512
- 卷头签名是不是 `OS64VOL1`

### 第二件：按扇区读取

现在已经有了一个最小接口：

```text
boot_volume_read_sector(volume, sector_index, buffer, buffer_size)
```

它做的事情很直接：

1. 检查 sector index 合不合法
2. 算出这一个扇区在内存里的偏移
3. 把这 512 字节拷到调用者给的缓冲区

### 第三件：把卷头信息暴露给 shell

现在 shell 里多了一个：

```text
disk
```

它可以让你直接看到：

- 启动卷起始 LBA
- 扇区数
- 扇区大小
- 卷签名
- 卷名

---

## 8. 这一轮怎么测试

### 正常启动链测试

运行：

```bash
make test-stage1
```

这一轮新的关键日志包括：

- `boot volume loaded ok`
- `boot_volume_ptr=0x...`
- `boot_volume_start_lba=...`
- `boot_volume_sector_count=4`
- `boot_volume_sector_size=512`
- `boot_volume_signature=OS64VOL1`
- `boot_volume_name=boot-volume`
- `boot_volume_readme=boot volume sector 1: hello from os64`
- `boot_volume_notes=boot volume sector 2: next step is filesystem`
- `disk read ok`

这几行连起来的意思是：

1. stage2 真的把卷读进来了
2. BootInfo 真的把交接信息传给 kernel 了
3. kernel 真的能按扇区再次读取这段卷数据

### 异常测试

运行：

```bash
make test-invalid-opcode
make test-page-fault
```

这两项继续通过，
说明你这次改 `BootInfo`、stage2、kernel 存储层时，
没有把原来的异常路径和启动主链打坏。

---

## 9. 这一步到底算不算“磁盘驱动”

严格说：

> 还不算。

因为真正读介质的动作仍然发生在：

- stage2
- 实模式
- BIOS `int 13h`

而内核现在做的是：

- 消费一段已经被搬进来的卷数据
- 提供 sector read 接口

但它又不是“假动作”，
因为从后面文件系统代码的视角看，
你已经第一次拥有了：

> 一条真实的、可以按扇区读取输入数据的内核接口

这正是这一步的价值。

---

## 10. 下一步最自然做什么

现在最自然的下一步就真的是：

> 在这个 boot volume 之上，开始做最小文件系统读取。

比如先做：

- 解析卷头
- 解析最小目录项
- 按名字找到一段数据
- 把“文件内容”读出来并打印

一句话总结：

> 这一轮不是“真正写完了硬盘驱动”，而是把“kernel 能按扇区读到一段真实卷数据”这条链先打通了。
