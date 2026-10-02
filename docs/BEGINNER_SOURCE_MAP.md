# 当前版本源码阅读地图

如果你看到许多目录却不知道从哪里开始，先拿一个刚运行过的命令追源码。完整解释与练习见 [从零主教程](./BEGINNER_TUTORIAL.md)；这里负责回答“我要研究这件事，应打开哪个文件”。

## 1. 按实际运行顺序定位

```text
宿主构建脚本生成镜像
→ QEMU BIOS → stage1 → stage2 → entry64 → kernel_main
→ 内存/中断/调度基础设施
→ RAM 自测 → ATA 数据盘挂载
→ Shell 与用户进程 → syscall → 文件层 → 磁盘
```

| 你想解释的现象 | 对应文件/函数 | 再往下看 |
| --- | --- | --- |
| make 产生什么 | scripts/build-stage1-image.sh | build-user.sh、make-volume.py、make-boot-image.py |
| stage1 ok | boot/stage1.asm 的 start | load_stage2、BIOS int13 |
| e820 / long mode ok | boot/stage2.asm | collect_e820、setup_page_tables、long_mode_start |
| 第一次进入 C++ | kernel/boot/entry64.asm | BootInfo、kernel_main |
| mem_free_pages | kernel/memory/page_allocator.cpp | E820 available/allocated 位图 |
| 为什么高物理页能访问 | kernel/memory/paging.cpp | paging_initialize_direct_map、paging_physical_pointer |
| 相同虚拟地址为何独立 | kernel/memory/address_space.cpp | clone/map/unmap/destroy、CR3 |
| kmalloc 怎样切小块 | kernel/memory/heap.cpp、kmemory.cpp | 空闲链分割/合并、页映射 |
| run 打印入口和退出码 | kernel/shell/shell.cpp 的 handle_run_command | scheduler_create_user_elf_thread |
| ELF 文件怎样变成内存 | kernel/task/elf_loader.cpp | PT_LOAD 校验、逐页分配与拷贝 |
| 用户堆能变大 | kernel/syscall/syscall.cpp 的 sys_brk | address_space_unmap_user_page |
| 程序从 main 开始 | user/start.asm | 初始 argc/argv 栈布局 |
| 用户 print 怎样到屏幕 | user/os64.hpp | syscall.cpp、SyscallContext 输出回调 |
| sleep / waitpid 不忙等 | kernel/task/scheduler.cpp | sleeping/blocked/ready 与唤醒 |
| 用户正在计算却被打断 | kernel/interrupts/interrupts.cpp 的 kernel_handle_irq | PIT、UserTrapFrame、切换汇编 |
| 键盘和串口文字输入 | keyboard.cpp、serial.cpp、console.cpp | IRQ 缓冲、read_stdin_stream |
| 文件跨重启存在 | kernel/storage/ata_pio.cpp | BlockDevice、FLUSH CACHE |
| 保存失败怎样保护原文 | kernel/fs/os64fs.cpp 的 mutate | staged_read/staged_write、校验/回滚 |
| edit 修改与保存 | user/programs/edit.cpp | memory.cpp、replace_file、sys_replace_file |
| 新工具安装保留笔记 | scripts/update-tools.py、tools/update_tools.cpp | OS64FS 副本验证、镜像替换 |

表里列的是仓库相对路径；可在代码编辑器按文件名打开，也可在宿主用 `rg 函数名 kernel user scripts` 搜索。函数行号会随修改改变，理解调用关系比记行号更可靠。

## 2. 第一次只读接口，再读实现

`.hpp` 通常定义结构、常量和函数声明；`.cpp` 实现具体动作。先读接口能知道一层收什么、还什么。例如：

- PageAllocator 收 BootInfo，分配时返回**物理地址**。
- paging 收虚拟/物理地址与权限，建立映射或给物理地址转换内核指针。
- AddressSpace 记录某进程页表根和用户窗口。
- PCB 收集进程资源，TCB 保存线程执行状态。
- BlockDevice 提供按扇区读/写/flush 回调，FS 不直接写设备端口。
- FileDescriptorTable 将小整数 fd 对应到文件句柄；每进程有自己的表。

看到同一个类型被多层包装，不要先问“为什么这么绕”，先问“这一层增加了什么状态或约定”。文件句柄增加当前位置，fd 表增加整数索引，syscall 增加权限边界，Shell 增加命令解释。

## 3. 三条推荐专题路线

### 从启动读到用户程序

1. stage1 的 start/load_stage2。
2. stage2 的 start、内存地图和模式切换。
3. entry64/kernel_main 的交接，注意 bootstrap 栈 `0x180000`。
4. direct map、物理页分配、私有 AddressSpace。
5. handle_run_command → ELF loader → scheduler 用户线程。
6. user/start.asm → main → int80 → exit/reap。

先看 [主教程第 5–8 章](./BEGINNER_TUTORIAL.md)，再用 [PROCESS_RUNTIME](./PROCESS_RUNTIME.md) 补精确边界。

### 从一个文件读到 ATA

1. 用户 cat 或 edit 的 open/read/replace_file。
2. syscall 的指针与路径检查。
3. fd → VFS → FileHandle / OS64FS。
4. inode/data bitmap 与目录项。
5. mutate 的暂存/提交/回滚。
6. BlockDevice → ATA 单扇区与 flush。

正式文件使用 ATA；BootVolume 仍用于早期 RAM 自测，不能据老目录地图推断“还没有磁盘驱动”。

### 从内存测试读到页回收

1. mem_test 的 brk 检查与 >1 MiB 分配。
2. memory::allocate/release 的块账本和分割合并。
3. sys_brk 扩张清零、缩小与失败撤销。
4. 地址空间 unmap/destroy 与空页表回收。
5. 物理页 available/allocated 位图与 free_page。

全 256 MiB 管理范围仍受实际 E820 可用内存约束；低 1 MiB 和 `0x170000`–`0x180000` bootstrap 栈另行保留。

## 4. 如何使用旧教程

[文档索引](./README.md) 按主题列出原专题。每篇开头都说明其历史阶段与当前差异：小 ELF、单页栈、低页池、只读 RAM 卷、全局 fd/cwd 这些早期选择后来已改变。原篇里的例题仍有助于学原理，但不能抄旧地址常量覆盖当前设计。

当前 Shell 提示符是 `os64 % `，已有 run/spawn/wait/回收与可写数据盘；主教程、PROCESS_RUNTIME 和 PERSISTENT_STORAGE 是当前使用基准。不要为了读懂代码先从 kernel_main 第 1 行读完；每次只追一条你能实际验证的链。
