# 系统启动、内存与用户 ABI 六图规格

本规格与 [SYSTEM_MEMORY_FUNCTIONS.md](../SYSTEM_MEMORY_FUNCTIONS.md)、[USER_ABI_FUNCTIONS.md](../USER_ABI_FUNCTIONS.md) 配套；图块与逐函数表中的 K/A/V/H/S/U 字母一致。仅画当前源码已实现机制。后续可做的无锁、每CPU分配器、TLB shootdown、COW、动态链接不画成现状。

所有图为直接 imagegen 生成的横向教学位图，约16:9，白底，3列2行六栏，浅蓝/绿/橙/紫/粉/蓝底、细圆角边框、大中文标题与(a)..(f)栏头；用短句、数字、方框、箭头表达机制，完整函数名留在 Markdown。使用字节/地址/页的真实尺度，不把区间画成额外可访问页。

## K：kernel-bringup.png

- (a) 启动入口：日志 → 本核TSS → IDT → BootInfo；BootInfo/E820是装载器提供信息，非内核自行发现所有硬件。TSS.RSP0是用户进入内核栈入口。
- (b) 内存顺序：E820 → 4KiB物理页 → 直接映射 → 内核堆/注册分配器；直接映射是地址通道，不等于分配物理页。
- (c) 用户程序准备：只读启动卷 → VFS/FD → int80 → ELF/ring3测试；文件描述符和用户空间先建立再运行程序。
- (d) 自检顺序：PIC/PIT100Hz → 调度器/输入/shell/用户/文件测试；自检使用启动卷夹具，持久数据盘在后面挂载；当前实现没有独立硬件看门狗。
- (e) 服务运行：重建正式调度器前先铺满内核堆；ATA数据盘优先/否则RAM盘 → shell/日志/网络 → BSP内核工作线程 → SMP启动 → 调度循环。用户线程可固定绑定AP，shell/网络内核线程仍在BSP。
- (f) 观察与失败：串口日志、VGA状态行、断言与错误日志；内核异常诊断后停止，用户异常交退出/回收；日志不是保证每个串口写都有超时。

## A：physical-pages.png

- (a) 从E820读可用RAM：usable且属性有效；仅整张4KiB页可用。例可用段[0x100003,0x103001)→完整页[0x101000,0x103000)。
- (b) 排除保留区：低1MiB、BSS[0x100000,0x160000)、启动栈[0x170000,0x180000)、启动卷；E820 reserved覆盖到的任意页均排除。不要暗示BSS保留之外的中间区永远不可用。
- (c) 位图找空页：候选=available & ~allocated；按word扫描，再ctz找最低置位，置allocated并减空闲数；无候选返回PA=0。
- (d) 所有权与归还：范围内/4KiB对齐/available/allocated四条件；通过才free并计数；重复free拒绝；free不保证清零。
- (e) PA不是指针：page PA → 直接映射VA → 内核读写；没有分配到的页不可当私有页；低256MiB映射窗口也不证明其中全是RAM。
- (f) 容量：仅管理低256MiB，共65536个4KiB页位；available与allocated各8KiB，共16KiB；统计O(1)，分配最坏扫描1024个64位word；不声称最快性能。

## V：paging-address-space.png

- (a) 地址拆解：PML4/PDPT/PD/PT四级各9bit + 12bit页内偏移；CR3提供根物理地址；读解析支持1GiB/2MiB大页和4KiB页。
- (b) 内核直接映射：旧低2MiB可访问区用于建立新表；ffff800000000000 + PA 映射低256MiB、128个2MiB叶，supervisor；映射不等于物理页可分配。
- (c) 创建4KiB映射：逐级缺表则分配并清零，父项传播U/W；拒绝冲突大页；失败撤销新表/恢复改过父权限；叶替换与用户wrapper的拒绝重复策略有区别。
- (d) 私有用户空间：深复制页表页；保留内核supervisor数据叶，丢弃原user叶；用户窗口[0x400000,0x800000)4MiB，各进程private；不是fork/COW。
- (e) 取消映射/回收：验证用户页→清叶→当前根本核invlpg→还物理页→删空页表；destroy不能销毁当前根。调度生命周期检查全CPU current；当前没有跨CPU TLB shootdown。
- (f) 权限/MMIO：NX仅CPU支持且EFER.NXE开后使用；MMIO高位规范supervisor映射，PA<4GiB，PCD/PWT；设备地址不从RAM页分配器获取。

## H：heap-memory.png

- (a) 内核堆窗口[0x01000000,0x01400000)4MiB；按需映射物理页，物理页从2MiB以上取，W/NX且清零；正式用户根克隆前heap_reserve铺满。
- (b) first-fit：空闲链中第一块能容纳的块；payload16字节对齐，分配头32字节、空闲头16字节；只在剩余能放空闲头时拆分；分配清请求字节。
- (c) free与合洞：按地址插入→合相邻空块；对象不搬家；free回块，不把内核堆映射页还PMM；坏magic/重复free拒绝。
- (d) C++对象层：kmalloc/kcalloc → knew先分配再构造 → kdelete先析构再释放；calloc乘法溢出检查；构造与“拿一段字节”含义不同。
- (e) 用户堆：32字节Block+16字节对齐；first-fit/brk扩展；release先标空并合洞，再尝试尾块缩brk归还整页；缩brk失败保留可复用空闲块，页面仍映射。非法或重复free由调用者避免；分配不自动清零。resize替换分配失败保留原已分配块的机制放Markdown中解释，不混作release的结果。
- (f) 衡量现状：used按对齐payload计，mapped按页计，free为块空间；有碎片且共用BKL，没有每CPU缓存/SLAB；不标榜实测速度。

## S：syscall-boundary.png

- (a) int0x80 ABI：RAX编号；RDI/RSI/RDX/RCX/R8五参数；RAX按signed64读返回；不是Linux syscall指令ABI。
- (b) 进入内核：ring3→本核TSS.RSP0→保存完整现场/BKL→校验→服务→恢复；interrupt gate令IF=0，原user IF=1才在服务期间开本核IRQ；CLI不是跨核锁。
- (c) 指针：read输出需要W，write输入需要可读；整段所有页逐级Present/U，写还需W；转成pointer不等于可信。普通传输≤69632，字符串63字节+NUL，UDP≤1200。
- (d) 路径/FD：cwd+相对路径→规范绝对路径；/、.、..处理，根不向上；公开fd0..2标准、3..18普通；内部slot16..18标准、0..15普通。
- (e) 等待：正式线程登记pipe/stdin/UDP/退出事件再阻塞切栈，恢复后重查；sys_waitpid=wait助手+reap助手，成功已回收；不要写公开waitpid只读。
- (f) 返回边界：普通0/-1..-7；UDP独立约定，0可为零长度包；brk失败返回旧break；无Linux兼容承诺、无无限长传输。

## U：elf-user-abi.png

- (a) ELF入口校验：ELF64/小端/x86_64/ET_EXEC；文件≤69632字节；program headers≤8；入口在可执行PT_LOAD；没有PIE/动态链接。
- (b) 先计划页：mem_size≥file_size；文件与用户范围界内；alignment为0/1允许，只有alignment>1时才要求2幂且VA与文件偏移同余；拒绝W+X、重叠页/已有映射；加载页总计≤256。
- (c) 再加载：每页清零并映射→拷贝文件字节→尾部BSS为零；只读执行页与写NX页分开；暂存缓冲RAII释放，半途映射失败由调用者销毁新空间。
- (d) 用户布局：低地址0x400000→ELF→image_end/heap→0x7EF000保护页→0x7F0000栈底→0x800000栈顶；窗口4MiB，stack64KiB向下，guard4KiB未映射；两个supervisor栈不属于这里。
- (e) brk：0查询；请求地址在heap_base..heap_limit；立即分配清零W/NX页；增长失败回滚本次新页、break不变；缩堆还整页并清保留页尾。
- (f) 用户包装：open19/brk20/pipe22/wait16/UDP36..40/SMP41 → int80；SMP snapshot128字节ABI1，UDP metadata12字节且reserved为0；数字是接口编号不是性能数据。
