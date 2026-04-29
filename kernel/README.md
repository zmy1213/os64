# kernel 目录说明

现在 `kernel/` 已经按职责拆成 5 类：

```text
kernel/
├── boot/
│   ├── boot_info.hpp
│   ├── entry64.asm
│   └── linker.ld
├── core/
│   └── kernel_main.cpp
├── interrupts/
│   ├── interrupts.hpp
│   ├── interrupts.cpp
│   ├── interrupt_stubs.asm
│   ├── pic.hpp
│   ├── pic.cpp
│   ├── pit.hpp
│   └── pit.cpp
├── memory/
│   ├── page_allocator.hpp
│   ├── page_allocator.cpp
│   ├── paging.hpp
│   ├── paging.cpp
│   ├── heap.hpp
│   └── heap.cpp
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
- 定时器中断测试
- 异常测试入口

一句话理解：

> `core/` 管“内核起来以后，先按什么顺序初始化自己”。

---

## 3. `interrupts/`

这里放和异常/中断入口有关的代码：

- `interrupt_stubs.asm`
  真正离 CPU 最近的汇编入口。
- `interrupts.cpp/.hpp`
  IDT、异常名字表、开关中断、通用 trap/IRQ 接入逻辑。
- `pic.cpp/.hpp`
  初始化 8259A PIC，把硬件 IRQ 重映射到 32~47，并在 IRQ 结束后发 EOI。
- `pit.cpp/.hpp`
  初始化 8253/8254 PIT，让 IRQ0 周期性地产生时钟 tick。

一句话理解：

> `interrupts/` 管“CPU 一出异常、硬件一来中断，先怎么接住它”。

---

## 4. `memory/`

这里放和内存管理直接有关的模块：

- `page_allocator.*`
  从 E820 usable 区域里分物理页。
- `paging.*`
  管页表和虚拟地址映射。
- `heap.*`
  在页表和物理页之上，提供更像“分配器”的堆接口。

一句话理解：

> `memory/` 管“物理内存怎么拿、虚拟地址怎么映射、堆内存怎么分”。

---

## 5. `runtime/`

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

## 6. 为什么现在这样分

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
