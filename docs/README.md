# 文档阅读顺序

这几份文档不是并列关系，而是有明显先后顺序的。

如果你现在是从零学这个项目，最合理的阅读顺序是：

1. [Stage1 写入说明](./STAGE1_WRITING_GUIDE.md)
   先理解整条启动链为什么从 `stage1` 开始，以及 boot sector 为什么只能做最小事情。
2. [Boot 寄存器小白说明](./BOOT_REGISTERS_BEGINNER.md)
   这是辅助材料，作用是先把后面汇编里反复出现的寄存器名字弄明白。
3. [Stage2 保护模式说明](./STAGE2_PROTECTED_MODE_GUIDE.md)
   这一篇开始进入真正的第二阶段初始化：A20、E820、GDT、保护模式。
4. [E820 逐行讲解版](./E820_LINE_BY_LINE_GUIDE.md)
   它不是独立阶段，而是 Stage2 里最关键、最容易看晕的一小段深挖。
5. [页表 + Long Mode 小白说明](./LONG_MODE_GUIDE.md)
   当你已经懂了保护模式，再继续看为什么还要有页表、PAE、LME 和 long mode。
6. [从 Long Mode 到 C++ 内核](./KERNEL_ENTRY_GUIDE.md)
   最后再看 bootloader 怎么把控制权交给真正的 64 位 C++ 内核。
7. [从 E820 到第一版页分配器](./E820_PAGE_ALLOCATOR_GUIDE.md)
   最后再看内核怎样把 BIOS 给的内存地图变成真正可分配的 4 KiB 页。

一句话记忆这个顺序：

```text
stage1
-> 先补寄存器基础
-> stage2 保护模式
-> 深挖 E820
-> 页表和 long mode
-> 进入 C++ 内核
-> 第一版页分配器
```
