# 设备 I/O 五张六栏图规格

对应 [DEVICES_IO_FUNCTIONS.md](../DEVICES_IO_FUNCTIONS.md) 的162个定义。每图使用白底、三列两行(a)–(f)、淡蓝/绿/橙/紫/粉圆角面板、粗黑中文总标题、短文字与有方向箭头。用内置imagegen直接生成；没有程序绘图。完整逐函数边界在MD，不强塞全部长函数名进图。

## I：console-input.png

标题“OS64 输入与行编辑”；副标题“字节变事件 · 等待后重试 · 草稿与显示分开”。

- (a) “两路输入”：PS/2 Set1扫描码→E0/Shift/Caps翻译；COM1字节→ESC状态机；两路汇入事件。键盘IRQ1、串口IRQ4。
- (b) “256格事件环”：character与arrow是事件；head/read和tail/write画不同位置，计数“总事件数”“字符数”；满环丢新事件、计数。不画256字节字符串。
- (c) “检查·登记·阻塞”：BKL+CLI下check empty→登记→重查→Blocked，事件唤醒→Ready→重试；console等任意事件，stdin等字符。Ready不保证拿到字符。
- (d) “草稿状态”：length/cursor/history_cursor；示例abc中间插x→axbc；Up保存草稿浏览历史，Down恢复。插删最坏O(N)。
- (e) “VGA 80×25”：字符+颜色16bit格，viewport列边界，溢行滚动；RSP0/TSS不在本图。重绘草稿与屏幕位置分开。
- (f) “串口同步与整行拒绝”：重绘→ANSI移动/清右侧；超capacity-1则消费到Enter→清草稿→拒绝整行。禁止截断执行。

边界：ASCII，不声称完整Unicode终端。stdin stream会忽略并消费编辑事件，不能画多消费者广播。CLI只本核，跨核靠BKL；正式线程不能持锁HLT等输入。

## H：interrupt-devices.png

标题“OS64 中断与设备发现”；副标题“先找到设备，再连接门铃与时间”。

- (a) PCI：bus/slot/function→0xCF8选择→0xCFC数据；16位写command不回写status W1C；穷举256×32×最多8。
- (b) PIC：16线→向量32..47；主IRQ2级联从片，mask0允许/1屏蔽；从片开线也开IRQ2。
- (c) PIT：1193182Hz÷divisor→默认100Hz；仅BSP全局tick+1，AP LAPIC只本核记账。
- (d) EOI：若设备需要，读状态清中断→从片（若来自从片）→主片EOI→可能调度；PIT无需读设备状态清源，网卡共享INTx不认领ISR=0。
- (e) 等待：毫秒ceil换tick，正式线程Sleeping/Blocked切走；启动无线程才HLT。AP不能持BKL等BSP时钟。
- (f) 边界：先IDT/TSS/控制器后STI；CLI只本核；有界轮询不是精确超时，PCI无热插拔。

禁止把PIC mask位反画，不把每颗AP的tick都加到系统时间，不写AP使用PIC时钟或已支持MSI-X多队列。

## S：storage-io.png

标题“OS64 存储 I/O 与持久性”；副标题“统一块接口 · 512B扇区 · RAM卷与磁盘分开”。

- (a) FS→BlockDevice回调表→BootVolume RAM / ATA PIO两个后端，相对扇区范围先验。
- (b) RAM卷：stage2预读RAM→每次复制512B；写只是改RAM，重启不持久。
- (c) ATA命令：合法LBA28→选盘/settle→等BSY=0→写LBA/单扇区命令→等DRQ=1；0x20读/0x30写。
- (d) 搬数据：0x1F0→256个16位word↔512B buffer；完成后验DRQ清与ERR/DF。
- (e) flush：write成功→需要时0xE7 FLUSH CACHE→完成；不等于FS断电原子事务。RAM卷flush无设备回调。
- (f) 错误边界：IDENTIFY拒ATAPI/非LBA/非512逻辑扇区；容量最多2^28扇区；最多1000000次poll，不无限卡死、不保证毫秒时延。

不要把BootVolume写画成ATA落盘，不画DMA/AHCI/NVMe；PIO同步忙等且IDE IRQ关闭。flush无journal恢复，不承诺断电文件永不损坏。

## V：virtio-dma.png

标题“OS64 Virtio DMA 队列”；副标题“物理地址 · 发布顺序 · 完成后回收 · BSP单RX队列”。

- (a) 连续ring：descriptor/available/used在物理连续2–3页；packet pages独立分配。虚拟连续不等于物理连续。
- (b) 每帧2描述符：page+0的10B header→page+64的Ethernet数据，54B间隙不传输；RX可写，TX只读。queue64→32帧，queue256→64帧。
- (c) 发布：数据+descriptor+avail槽→MFENCE→avail.index→MFENCE→notify；不得index先于数据。
- (d) 完成：设备写used→CPU读index+LFENCE→校验head/长度→TX清in-flight；RX同步callback→重投descriptor。索引16位回绕。
- (e) IRQ与预算：INTx只ack+wake→BSP worker每次budget32→还有数据yield，否则CLI检查/Block；AP UDP用户读软件inbox，非独立RX DMA。
- (f) 安全边界：legacy MAC-only、无卸载；不合法used导致fail_device；保留可能仍DMA的页，等待重启；reset确认停DMA才能释放初始化失败的页。

head和数据描述符是链条，不是一次1514B零拷贝用户包。不能画返回callback后指针仍归用户、使用未完成TX槽、设备失败后立即free。最大帧1514B；header不把数据offset64画成header长度64。

## O：logging-observability.png

标题“OS64 日志与性能观测”；副标题“固定记录 · 序号环 · 增量读取 · 指标有边界”。

- (a) 128B记录：seq/tick/level/component16/message88/reserved；文本最多15/87字节+NUL，不按Unicode字符数解释。writer不分配、不阻塞、不串口打印；BKL+CLI保护。
- (b) 256条环：slot=(sequence-1)%256，sequence从1递增；画写新格，不把条数容量写成字节容量。
- (c) 满后覆盖：第257条覆盖第1条，overwritten+1；新窗口2..257，不是自动存磁盘。
- (d) 增量读：after_sequence→仍保留的最新窗口→最多capacity条升序拷贝；丢失旧记录用计数解释。
- (e) 快照：128B ABI v1，PIT/调度/空页/heap/日志/CPU/TSC；TSC为原始计数、不是纳秒；在线核数不等于速度倍数。
- (f) 字节工具：memset填、memcpy非重叠、memmove重叠时选方向、memcmp首差；逐字节O(N)，不是最快/零拷贝。

不要把log ring画成持久磁盘journal；仅进程生命周期写事件，不每tick输出。component与message容量含结束符。CLI不取跨核锁；TSC未校准/未保证跨核同步。
