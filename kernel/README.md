# kernel 目录说明

现在 `kernel/` 已经按职责拆成 10 类：

```text
kernel/
├── boot/
│   ├── boot_info.hpp
│   ├── entry64.asm
│   └── linker.ld
├── console/
│   ├── console.hpp
│   └── console.cpp
├── core/
│   └── kernel_main.cpp
├── fs/
│   ├── os64fs.hpp
│   ├── os64fs.cpp
│   ├── file.hpp
│   ├── file.cpp
│   ├── directory.hpp
│   ├── directory.cpp
│   ├── vfs.hpp
│   ├── vfs.cpp
│   ├── fd.hpp
│   └── fd.cpp
├── interrupts/
│   ├── interrupts.hpp
│   ├── interrupts.cpp
│   ├── interrupt_stubs.asm
│   ├── pic.hpp
│   ├── pic.cpp
│   ├── pit.hpp
│   ├── pit.cpp
│   ├── keyboard.hpp
│   └── keyboard.cpp
├── memory/
│   ├── page_allocator.hpp
│   ├── page_allocator.cpp
│   ├── paging.hpp
│   ├── paging.cpp
│   ├── heap.hpp
│   ├── heap.cpp
│   ├── kmemory.hpp
│   └── kmemory.cpp
├── storage/
│   ├── boot_volume.hpp
│   ├── boot_volume.cpp
│   ├── block_device.hpp
│   └── block_device.cpp
├── shell/
│   ├── shell.hpp
│   └── shell.cpp
├── syscall/
│   ├── syscall.hpp
│   └── syscall.cpp
└── runtime/
    ├── runtime.hpp
    └── runtime.cpp
```

---

## 1. `boot/`

这里放“真正和启动入口强绑定”的文件：

- `entry64.asm`
  long mode 以后，CPU 第一次进入 64 位内核时先落到这里。
- `boot_info.hpp`
  这是 stage2 交给内核的最小启动信息结构。
- `linker.ld`
  负责决定内核各段最终被链接到什么地址。

一句话理解：

> `boot/` 管“内核怎么被启动起来”。

---

## 2. `core/`

这里先放内核主控入口：

- `kernel_main.cpp`

它现在负责：

- 串口/VGA 状态输出
- BootInfo 检查
- E820 打印
- 页分配器初始化
- 页表测试
- 堆测试
- boot volume / 原始块设备测试
- 只读文件系统挂载和路径读取测试
- 文件句柄 open/read/close/stat 测试
- 目录句柄 open/read/rewind/close 测试
- VFS mount/stat/open/read/close 测试
- 文件描述符表 fd_open/fd_read/fd_close 测试
- 系统调用外观 sys_open/sys_read/sys_write/sys_close 测试
- 第一版 `int 0x80` 软中断 syscall 烟测
- 定时器中断测试
- 基于 tick 的最小等待 / sleep 测试
- 键盘 IRQ + 字符缓冲区测试
- 控制台回显 + 最小行输入测试
- 最小 shell 命令测试
- shell 当前工作目录 `pwd` / `cd` / 相对路径测试
- 正常启动后的最小交互 shell 循环
- 异常测试入口

一句话理解：

> `core/` 管“内核起来以后，先按什么顺序初始化自己”。

---

## 3. `console/`

这里放最小控制台模块：

- `console.cpp/.hpp`
  负责 VGA 控制台字符输出、退格、换行，以及第一版行输入接口。

一句话理解：

> `console/` 管“字符怎么显示出来、怎么被拼成一整行输入”。

---

## 4. `interrupts/`

这里放和异常/中断入口有关的代码：

- `interrupt_stubs.asm`
  真正离 CPU 最近的汇编入口。
- `interrupts.cpp/.hpp`
  IDT、异常名字表、开关中断、通用 trap/IRQ 接入逻辑。
- `pic.cpp/.hpp`
  初始化 8259A PIC，把硬件 IRQ 重映射到 32~47，并在 IRQ 结束后发 EOI。
- `pit.cpp/.hpp`
  初始化 8253/8254 PIT，让 IRQ0 周期性地产生时钟 tick，并提供最小 `wait/sleep` 接口。
- `keyboard.cpp/.hpp`
  读取 IRQ1 键盘扫描码，翻译最小字符集，并把字符放进环形缓冲区。

一句话理解：

> `interrupts/` 管“CPU 一出异常、硬件一来中断，先怎么接住它”。

---

## 5. `memory/`

这里放和内存管理直接有关的模块：

- `page_allocator.*`
  从 E820 usable 区域里分物理页。
- `paging.*`
  管页表和虚拟地址映射。
- `heap.*`
  在页表和物理页之上，提供更像“分配器”的堆接口。
- `kmemory.*`
  在堆之上再包一层正式入口，给后面的 C++ 对象分配提供 `kmalloc/kfree/knew/kdelete`。

一句话理解：

> `memory/` 管“物理内存怎么拿、虚拟地址怎么映射、堆内存怎么分，以及对象怎么落到堆上”。

---

## 6. `fs/`

这里放第一版文件系统模块：

- `os64fs.cpp/.hpp`
  负责只读 `OS64FS v1` 的挂载、路径查找、目录遍历和文件读取。
- `file.cpp/.hpp`
  在 `OS64FS` 上面提供第一版文件句柄接口，包括 `file_open`、`file_read`、`file_close` 和 `file_stat`。
- `directory.cpp/.hpp`
  在 `OS64FS` 上面提供第一版目录句柄接口，包括 `directory_open`、`directory_read`、`directory_rewind` 和 `directory_close`。
- `vfs.cpp/.hpp`
  在文件句柄和目录句柄上面提供第一版 VFS 入口，包括 `vfs_stat`、`vfs_open_file`、`vfs_open_directory` 等接口。
- `fd.cpp/.hpp`
  在 VFS 上面提供第一版文件描述符表，包括 `fd_open`、`fd_read`、`fd_seek`、`fd_stat` 和 `fd_close`。

一句话理解：

> `fs/` 管“原始块数据怎样被解释成目录、文件和路径，并把底层 inode / 目录项逐层包成 VFS 和 fd 这种更接近真实 OS 的访问入口”。

---

## 7. `shell/`

这里放最小 shell 模块：

- `shell.cpp/.hpp`
  负责提示符、命令解析、内建命令执行，以及第一版交互循环。
  现在已经能通过 `pwd` / `cd` 管当前目录，通过 `ls` / `cat` / `stat` 观察文件系统，其中 `ls` / `stat` 走 VFS，`cat` 进一步走 fd 表。

一句话理解：

> `shell/` 管“读到一整行之后，系统到底要怎么解释并执行它，包括当前目录和相对路径这种交互状态”。

---

## 8. `storage/`

这里放和“启动介质数据如何被内核读取”有关的代码：

- `boot_volume.cpp/.hpp`
  负责表示 stage2 预读进内存的一段原始连续扇区。
- `block_device.cpp/.hpp`
  再往上一层，把 `BootVolume` 包装成统一的块设备读接口。

一句话理解：

> `storage/` 管“内核现在怎样先拿到一段原始块设备数据，即使还没有真正的磁盘控制器驱动”。

---

## 9. `syscall/`

这里放第一版系统调用外观：

- `syscall.cpp/.hpp`
  现在先提供 `SyscallContext`，以及 `sys_open`、`sys_read`、`sys_write`、`sys_stat`、`sys_seek`、`sys_close`，再往前补了 `sys_getcwd`、`sys_chdir`、`sys_stat_path`、`sys_listdir`。
  它先把“上层通过 fd、cwd、路径、错误码访问内核服务”的形状定下来，后来又往前补了第一版 `int 0x80` 软中断分发器，以及公开 syscall fd `0/1/2 = stdin/stdout/stderr`、`3+ = 普通文件` 这层边界翻译。

一句话理解：

> `syscall/` 管“以后用户程序请求内核服务时，先看到什么统一入口，这些调用共享哪些上下文状态，以及第一版 CPU 软中断入口怎么把寄存器转进这些服务”。

---

## 10. `runtime/`

这里放 freestanding 内核里最小的运行时工具：

- `runtime.cpp/.hpp`

现在先只有：

- `memory_set`
- `memory_copy`

后面如果再补：

- `memory_compare`
- `string_length`
- 其它最小 libc 替代

也会继续放这里。

一句话理解：

> `runtime/` 管“没有宿主 libc 时，内核自己最基础的工具函数”。

---

## 11. 为什么现在这样分

因为如果所有文件都继续平铺在 `kernel/` 根目录，
后面一多起来你会很快分不清：

- 哪些是启动相关
- 哪些是内存管理
- 哪些是中断/异常
- 哪些只是基础工具函数

现在这套分类方式的目标不是“追求复杂目录层级”，
而是让你一眼就能先按职责找到文件。

一句话总结：

> 现在 `kernel/` 不是按“文件类型”分，而是按“这个模块在内核里负责什么”分。
