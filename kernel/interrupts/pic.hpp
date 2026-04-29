#ifndef OS64_PIC_HPP
#define OS64_PIC_HPP

#include <stdbool.h>
#include <stdint.h>

// 初始化 8259A PIC，并把原来的 IRQ 向量重映射到 32~47。
// 这样做的原因是：
// 1. CPU 自己把 0~31 留给异常
// 2. 如果 PIC 还用老默认值，就会和异常向量撞车
bool initialize_pic();

// 打开某一路 IRQ，比如：
// 0 = PIT 定时器
// 1 = 键盘
bool enable_pic_irq(uint8_t irq_line);

// 关闭某一路 IRQ。
bool disable_pic_irq(uint8_t irq_line);

// IRQ 处理完后，必须给 PIC 发送 EOI。
// 不然 PIC 会以为这次中断还没结束，后面的中断可能就不再继续送进来。
void send_pic_eoi(uint8_t vector);

#endif
