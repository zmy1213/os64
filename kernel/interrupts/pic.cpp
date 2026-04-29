#include "interrupts/pic.hpp"

#include "interrupts/interrupts.hpp"

namespace {

constexpr uint16_t kPic1CommandPort = 0x20;   // 主 PIC 命令端口。
constexpr uint16_t kPic1DataPort = 0x21;      // 主 PIC 数据端口。
constexpr uint16_t kPic2CommandPort = 0xA0;   // 从 PIC 命令端口。
constexpr uint16_t kPic2DataPort = 0xA1;      // 从 PIC 数据端口。
constexpr uint8_t kPicInitialize = 0x11;      // ICW1：边沿触发 + 需要 ICW4 + 初始化命令。
constexpr uint8_t kPic8086Mode = 0x01;        // ICW4：告诉 PIC 按 8086/88 模式工作。
constexpr uint8_t kPicEoi = 0x20;             // 非特定 EOI，表示“这个中断处理完了”。
constexpr uint8_t kMasterCascadeIrqLine = 0x04; // ICW3：主片的 IRQ2 连着从片。
constexpr uint8_t kSlaveCascadeIdentity = 0x02; // ICW3：从片挂在主片的 IRQ2 上。
constexpr uint8_t kMasterMaskAllowTimerOnly = 0xFE; // 11111110，只放开 IRQ0 定时器。
constexpr uint8_t kSlaveMaskAllBlocked = 0xFF;      // 从片先全部屏蔽，后面接键盘等再慢慢开。

inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void io_wait() {
  // 往 0x80 这个历史上常用的“延迟端口”写一个无意义字节，
  // 只是为了给比较老的硬件一点点 I/O 间隔时间。
  asm volatile("outb %%al, $0x80" : : "a"(0));
}

}  // namespace

bool initialize_pic() {
  // 第 1 步：告诉主片和从片“我要开始重新初始化你们了”。
  out8(kPic1CommandPort, kPicInitialize);
  io_wait();
  out8(kPic2CommandPort, kPicInitialize);
  io_wait();

  // 第 2 步：给它们新的中断向量基址。
  // 以后 IRQ0~15 就不再占用老默认向量，而是改成 32~47。
  out8(kPic1DataPort, kPicMasterVectorBase);  // 主片 IRQ0~7 -> 32~39。
  io_wait();
  out8(kPic2DataPort, kPicSlaveVectorBase);   // 从片 IRQ8~15 -> 40~47。
  io_wait();

  // 第 3 步：告诉主从片它们是怎么级联连接的。
  out8(kPic1DataPort, kMasterCascadeIrqLine);
  io_wait();
  out8(kPic2DataPort, kSlaveCascadeIdentity);
  io_wait();

  // 第 4 步：告诉 PIC 之后按 8086/88 兼容模式工作。
  out8(kPic1DataPort, kPic8086Mode);
  io_wait();
  out8(kPic2DataPort, kPic8086Mode);
  io_wait();

  // 第 5 步：先把所有 IRQ 都屏蔽住，只单独放开 IRQ0 定时器。
  // 这样这一轮只会收到时钟中断，不会一下子把键盘等别的硬件也引进来。
  out8(kPic1DataPort, kMasterMaskAllowTimerOnly);
  out8(kPic2DataPort, kSlaveMaskAllBlocked);
  return true;
}

void send_pic_eoi(uint8_t vector) {
  if (vector >= kPicSlaveVectorBase &&
      vector < kPicSlaveVectorBase + kHardwareIrqCount - 8) {
    out8(kPic2CommandPort, kPicEoi); // 如果来自从片，先告诉从片“处理完了”。
  }

  // 不管中断来自主片还是从片，最后都必须回主片发一次 EOI。
  out8(kPic1CommandPort, kPicEoi);   // 最后总要告诉主片“这次 IRQ 处理结束了”。
}
