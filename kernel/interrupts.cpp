#include "interrupts.hpp"

#include "runtime.hpp"

namespace {

constexpr uint16_t kIdtEntryCount = 256;       // x86_64 IDT 固定有 256 个向量槽位。
constexpr uint16_t kKernelCodeSelector = 0x18; // 这个值对应 stage2 里 GDT 的 64 位代码段。
constexpr uint8_t kInterruptGateType = 0x8E;   // P=1, DPL=0, Type=1110，表示内核态中断门。
constexpr const char* kExceptionNames[kCpuExceptionCount] = {
    "divide error",                    // 0
    "debug",                           // 1
    "non-maskable interrupt",          // 2
    "breakpoint",                      // 3
    "overflow",                        // 4
    "bound range exceeded",            // 5
    "invalid opcode",                  // 6
    "device not available",            // 7
    "double fault",                    // 8
    "coprocessor segment overrun",     // 9
    "invalid tss",                     // 10
    "segment not present",             // 11
    "stack segment fault",             // 12
    "general protection fault",        // 13
    "page fault",                      // 14
    "reserved",                        // 15
    "x87 floating-point exception",    // 16
    "alignment check",                 // 17
    "machine check",                   // 18
    "simd floating-point exception",   // 19
    "virtualization exception",        // 20
    "control protection exception",    // 21
    "reserved",                        // 22
    "reserved",                        // 23
    "reserved",                        // 24
    "reserved",                        // 25
    "reserved",                        // 26
    "reserved",                        // 27
    "hypervisor injection exception",  // 28
    "vmm communication exception",     // 29
    "security exception",              // 30
    "reserved",                        // 31
};

struct IdtEntry {
  uint16_t offset_low;     // 处理函数地址的低 16 位。
  uint16_t selector;       // 代码段选择子，这里固定指向 64 位代码段。
  uint8_t ist;             // 这一轮先不用 IST，所以填 0。
  uint8_t type_attributes; // 门类型、特权级、present 位都放在这里。
  uint16_t offset_mid;     // 处理函数地址的中间 16 位。
  uint32_t offset_high;    // 处理函数地址的高 32 位。
  uint32_t reserved;       // Intel 规定这里必须为 0。
} __attribute__((packed));

struct IdtPointer {
  uint16_t limit;          // IDT 总字节数减 1。
  uint64_t base;           // IDT 在内存里的起始地址。
} __attribute__((packed));

extern "C" uintptr_t isr_stub_table[kCpuExceptionCount];

IdtEntry g_idt[kIdtEntryCount];        // 把整张 IDT 先放在内核自己的静态内存里。

void set_idt_gate(uint8_t vector, void (*handler)()) {
  const uint64_t handler_address =
      reinterpret_cast<uint64_t>(handler);

  g_idt[vector].offset_low =
      static_cast<uint16_t>(handler_address & 0xFFFF);
  g_idt[vector].selector = kKernelCodeSelector;
  g_idt[vector].ist = 0;
  g_idt[vector].type_attributes = kInterruptGateType;
  g_idt[vector].offset_mid =
      static_cast<uint16_t>((handler_address >> 16) & 0xFFFF);
  g_idt[vector].offset_high =
      static_cast<uint32_t>((handler_address >> 32) & 0xFFFFFFFF);
  g_idt[vector].reserved = 0;
}

void load_idt(const IdtPointer* idt_pointer) {
  asm volatile("lidt (%0)" : : "r"(idt_pointer));
}

}  // namespace

bool initialize_idt() {
  memory_set(g_idt, 0, sizeof(g_idt));    // 先把所有槽位清零，避免脏数据进入 CPU。

  for (uint8_t vector = 0; vector < kCpuExceptionCount; ++vector) {
    set_idt_gate(vector, reinterpret_cast<void (*)()>(isr_stub_table[vector]));
  }

  IdtPointer idt_pointer{};
  idt_pointer.limit = static_cast<uint16_t>(sizeof(g_idt) - 1);
  idt_pointer.base = reinterpret_cast<uint64_t>(&g_idt[0]);

  load_idt(&idt_pointer);                           // 真正把 IDT 地址告诉 CPU。
  return true;
}

const char* exception_name(uint64_t vector) {
  if (vector < kCpuExceptionCount) {
    return kExceptionNames[vector];
  }

  return "unknown exception";
}
