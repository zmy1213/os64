# 从对象分配到原始 Boot Volume + 块设备入口

这一步是“文件系统之前的最后一层台阶”。

如果说上一轮你已经有了：

- 页分配器
- 页表
- 内核堆
- `kmalloc` / `knew`
- 可交互 shell

那这一轮真正要解决的问题就是：

> 64 位内核到底怎样先拿到一段稳定可读的块设备数据。

---

## 1. 为什么这一步还不直接写真正磁盘驱动

因为你现在的启动介质还是软盘镜像，
而 boot 阶段读盘靠的是 BIOS `int 13h`。

一旦进入：

- 保护模式
- long mode
- 64 位 C++ 内核

你就不能继续把 BIOS 中断当成长期方案了。

但如果这时直接跳去写真正硬件驱动，
你又会立刻撞上：

- FDC / ATA 寄存器协议
- 轮询和状态机
- DMA
- IRQ
- 超时恢复

这对“小白第一次把文件系统跑起来”来说太重了。

所以这一轮故意采用更稳的过渡方案：

> 让 stage2 先把一小段连续扇区读进内存，再由 64 位内核把它当成原始块设备入口。

---

## 2. 这一步到底做了什么

这一轮链路是：

```text
disk image
-> stage2 在实模式下读一段连续扇区
-> BootInfo 把内存地址 + 扇区信息交给内核
-> kernel 里的 BootVolume 表示这段原始数据
-> BlockDevice 提供统一 sector read 接口
```

也就是说，
这里解决的还不是“目录怎么找”，
而是先解决：

> 上层以后怎么稳定地按扇区读取一段块数据。

---

## 3. BootVolume 现在表示什么

现在的 `BootVolume` 不再是“自定义卷头 + 两段演示文本”。

它现在表示的是：

> stage2 预读到内存里的一段原始连续扇区。

里面保存的核心信息只有：

- `base`
- `start_lba`
- `sector_count`
- `sector_size`
- `ready`

它不负责解释“这里面到底是什么文件系统”，
它只负责回答一个问题：

> 这段原始块数据在哪里，我能不能按扇区把它读出来。

---

## 4. 为什么还要再包一层 BlockDevice

这一步非常关键。

如果文件系统以后直接依赖：

```text
boot_volume_read_sector(...)
```

那文件系统代码会被死死绑在“当前这个 boot 阶段的特殊来源”上。

所以这一轮加了一层：

```text
BlockDevice
```

它只暴露这些最小字段：

- 起始 LBA
- 扇区总数
- 扇区大小
- `read_sector`

这样做的意义是：

> 文件系统只面向“块设备接口”，不面向“某个特殊 bootloader 过渡结构”。

以后底层即使换成：

- 真正 ATA 驱动
- AHCI 驱动
- 别的启动器交接方式

上层文件系统也可以尽量不动。

---

## 5. BootInfo 为什么还要继续传这些字段

现在 `BootInfo` 里和存储相关的字段有：

- `boot_volume_ptr`
- `boot_volume_start_lba`
- `boot_volume_sector_count`
- `boot_volume_sector_size`

它们的作用分别是：

- 这段预读数据在内存哪里
- 这段数据原来在磁盘从哪里开始
- 一共读了多少扇区
- 每个扇区多大

你可以把它理解成：

```text
stage2 = 搬运工
BootInfo = 交接单
kernel = 真正开始消费数据的人
```

---

## 6. 这一轮内核多了什么能力

### 第一层能力：知道有一段原始块数据

内核现在已经能确认：

- stage2 的 boot volume 预读成功
- 64 位内核拿到了正确指针
- 扇区数量和大小也对

### 第二层能力：按扇区读取

内核现在可以通过：

```text
block_device_read_sector(...)
```

去读取这段数据。

### 第三层能力：和上层解耦

文件系统以后只需要看到一个：

```text
BlockDevice
```

而不需要知道底层是 stage2 预读来的。

---

## 7. 这一轮怎么测试

运行：

```bash
make test-stage1
```

现在会检查这些关键日志：

- `boot volume loaded ok`
- `boot_volume_ptr=0x...`
- `boot_volume_start_lba=...`
- `boot_volume_sector_count=4`
- `boot_volume_sector_size=512`
- `block_device_total_bytes=2048`
- `boot volume ok`

这说明：

> 从 stage2 到 64 位内核的“原始块设备入口”已经打通。

---

## 8. 这一步和下一步是什么关系

这一步只解决：

> “原始块数据怎么交进内核。”

下一步才解决：

> “内核怎么把这些块解释成 superblock、inode、目录项和文件内容。”

也就是：

```text
这一轮：raw sectors
下一轮：filesystem
```

一句话总结：

> `BootVolume` 现在不再是假文件系统，而是第一版真正文件系统之前的原始块设备桥接层。
