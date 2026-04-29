#include <stddef.h>
#include <stdint.h>

#include "boot_info.hpp"

namespace {

constexpr uint16_t kVgaColumns = 80;          // VGA 文本模式一行 80 列。
constexpr uintptr_t kVgaBase = 0xB8000;       // VGA 文本缓冲区从这个物理地址开始。
constexpr uint16_t kCom1Base = 0x3F8;         // COM1 串口的标准 I/O 端口基址。
constexpr uint8_t kTextColor = 0x0F;          // 白字黑底，和前面的 stage2 风格保持一致。

// 往 I/O 端口写一个字节。内核里没有现成库函数，所以这里自己直接发机器指令。
inline void out8(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// 从 I/O 端口读一个字节。串口状态轮询要靠它。
inline uint8_t in8(uint16_t port) {
  uint8_t value = 0;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

// 串口发送前先等发送保持寄存器空出来。
void serial_write_char(char ch) {
  while ((in8(kCom1Base + 5) & 0x20) == 0) {
  }

  out8(kCom1Base, static_cast<uint8_t>(ch));
}

// 把一个 0 结尾字符串送到串口，方便自动测试直接抓日志。
void serial_write_string(const char* text) {
  for (size_t i = 0; text[i] != '\0'; ++i) {
    serial_write_char(text[i]);
  }
}

// 直接往 VGA 文本缓冲区写一整行，让你在 QEMU 图形窗口里也能看到结果。
void vga_write_line(uint16_t row, const char* text, uint8_t color) {
  volatile uint16_t* const vga =
      reinterpret_cast<volatile uint16_t*>(kVgaBase);

  for (size_t col = 0; col < kVgaColumns; ++col) {
    const char ch = text[col];
    if (ch == '\0') {
      break;
    }

    vga[row * kVgaColumns + col] =
        static_cast<uint16_t>(color) << 8 | static_cast<uint8_t>(ch);
  }
}

// 既写屏幕也写串口，这样手工看和自动测都能兼顾。
void write_status_line(uint16_t row, const char* text) {
  vga_write_line(row, text, kTextColor);
  serial_write_string(text);
  serial_write_string("\r\n");
}

}  // namespace

extern "C" void kernel_main(const BootInfo* boot_info) {
  write_status_line(4, "hello from os64 kernel");

  if (boot_info != nullptr && boot_info->magic == kBootInfoMagic &&
      boot_info->memory_map_ptr != 0 &&
      boot_info->memory_map_entry_size == 24) {
    write_status_line(5, "boot info ok");
    return;
  }

  write_status_line(5, "boot info bad");
}
