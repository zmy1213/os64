#include "interrupts/interrupts.hpp"

#include "boot/segments.hpp"
#include "interrupts/keyboard.hpp"
#include "interrupts/pic.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint16_t kIdtEntryCount = 256;       // x86_64 IDT 固定有 256 个向量槽位。
constexpr uint8_t kInterruptGateType = 0x8E;   // P=1, DPL=0, Type=1110，表示内核态中断门。
constexpr uint8_t kUserInterruptGateType = 0xEE;  // P=1, DPL=3, Type=1110，允许以后 ring 3 也能显式触发。
constexpr size_t kKernelGdtQwordCount = 9;     // 5 个旧描述符 + 64 位 TSS(2 槽) + user data + user code。
constexpr size_t kKernelTssStackBytes = 8192;  // 第一版先给 RSP0 和 double-fault IST 各留 8 KiB 栈。
constexpr uint8_t kDoubleFaultVector = 8;      // double fault 是 CPU 异常向量 8。
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

// 64 位 TSS 的布局和 32 位时代不一样：
// 现在最关键的是：
// - `rsp0`: 用户态 ring 3 进入内核 ring 0 时，CPU 应该切到哪根内核栈
// - `ist1`: 如果 double fault 发生，CPU 还能切到一根单独的应急栈
struct TaskStateSegment64 {
  uint32_t reserved0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserved1;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t io_map_base;
} __attribute__((packed));

static_assert(sizeof(TaskStateSegment64) == 104,
              "64-bit TSS must stay 104 bytes");

struct GdtPointer {
  uint16_t limit;          // GDT 总字节数减 1。
  uint64_t base;           // GDT 在线性地址空间里的起始地址。
} __attribute__((packed));

extern "C" uintptr_t isr_stub_table[kCpuExceptionCount];
extern "C" uintptr_t irq_stub_table[kHardwareIrqCount];
extern "C" void syscall_interrupt_stub();

IdtEntry g_idt[kIdtEntryCount];        // 把整张 IDT 先放在内核自己的静态内存里。
TaskStateSegment64 g_kernel_tss;       // 第一版内核 TSS。先只开最关键的 RSP0 + IST1。
alignas(16) uint8_t g_kernel_tss_rsp0_stack[kKernelTssStackBytes];
alignas(16) uint8_t g_kernel_double_fault_ist_stack[kKernelTssStackBytes];
alignas(16) uint64_t g_kernel_gdt[kKernelGdtQwordCount];
bool g_tss_ready = false;
uint64_t g_kernel_tss_default_rsp0 = 0;  // 先记住“系统最初那根通用 RSP0”，后面非 user thread 运行时可以回退到它。

void set_idt_gate(uint8_t vector, void (*handler)(),
                  uint8_t type_attributes = kInterruptGateType,
                  uint8_t ist_index = 0) {
  const uint64_t handler_address =
      reinterpret_cast<uint64_t>(handler);

  g_idt[vector].offset_low =
      static_cast<uint16_t>(handler_address & 0xFFFF);
  g_idt[vector].selector = kKernelCodeSelector;
  g_idt[vector].ist = static_cast<uint8_t>(ist_index & 0x07);
  g_idt[vector].type_attributes = type_attributes;
  g_idt[vector].offset_mid =
      static_cast<uint16_t>((handler_address >> 16) & 0xFFFF);
  g_idt[vector].offset_high =
      static_cast<uint32_t>((handler_address >> 32) & 0xFFFFFFFF);
  g_idt[vector].reserved = 0;
}

void load_idt(const IdtPointer* idt_pointer) {
  asm volatile("lidt (%0)" : : "r"(idt_pointer));
}

uint64_t stack_top_address(const uint8_t* stack, size_t bytes) {
  return reinterpret_cast<uint64_t>(stack) + bytes;
}

void build_kernel_gdt() {
  memory_set(g_kernel_gdt, 0, sizeof(g_kernel_gdt));

  // 前 5 项先和 stage2 保持完全兼容：
  // 0 = null
  // 1 = 32 位代码段
  // 2 = 32 位数据段
  // 3 = 64 位代码段
  // 4 = 64 位数据段
  g_kernel_gdt[0] = 0x0000000000000000ULL;
  g_kernel_gdt[1] = 0x00cf9a000000ffffULL;
  g_kernel_gdt[2] = 0x00cf92000000ffffULL;
  g_kernel_gdt[3] = 0x00af9a000000ffffULL;
  g_kernel_gdt[4] = 0x00cf92000000ffffULL;

  // 64 位 TSS 描述符一共占 16 字节，也就是两个 GDT 槽位。
  // type=0x9 表示 available 64-bit TSS；P=1 表示描述符有效。
  const uint64_t base = reinterpret_cast<uint64_t>(&g_kernel_tss);
  const uint32_t limit = sizeof(g_kernel_tss) - 1;

  g_kernel_gdt[5] =
      static_cast<uint64_t>(limit & 0xFFFF) |                     // limit[15:0]
      ((base & 0xFFFFFFULL) << 16) |                              // base[23:0]
      (static_cast<uint64_t>(0x89) << 40) |                       // type=0x9, present=1
      (static_cast<uint64_t>((limit >> 16) & 0x0F) << 48) |       // limit[19:16]
      (static_cast<uint64_t>((base >> 24) & 0xFF) << 56);         // base[31:24]
  g_kernel_gdt[6] = base >> 32;                                   // base[63:32]

  // ring 3 段这一步先只做成最经典的平坦段：
  // - user data: type=0xF2
  // - user code: type=0xFA, L=1
  //
  // 这样第一次 `iretq` 落到 CPL=3 时，CPU 至少已经有合法的用户代码段/数据段可用。
  g_kernel_gdt[7] = 0x00cff2000000ffffULL;
  g_kernel_gdt[8] = 0x00affa000000ffffULL;
}

void load_kernel_gdt_and_tss(const GdtPointer* gdt_pointer) {
  const uint64_t code_selector = kKernelCodeSelector;

  // 这一段做 4 件事：
  // 1. `lgdt`：把 CPU 指向内核自己这份 GDT
  // 2. `lretq`：真正刷新 CS，让当前 64 位代码段也来自新 GDT
  // 3. 重装 DS/ES/SS/FS/GS：让数据段缓存也切到新 GDT
  // 4. `ltr`：把 TSS 选择子装进 task register
  asm volatile(
      "lgdt (%[gdt])\n\t"
      "pushq %[code_selector]\n\t"
      "leaq 1f(%%rip), %%rax\n\t"
      "pushq %%rax\n\t"
      "lretq\n\t"
      "1:\n\t"
      "movw %[data_selector], %%ax\n\t"
      "movw %%ax, %%ds\n\t"
      "movw %%ax, %%es\n\t"
      "movw %%ax, %%ss\n\t"
      "movw %%ax, %%fs\n\t"
      "movw %%ax, %%gs\n\t"
      "movw %[tss_selector], %%ax\n\t"
      "ltr %%ax\n\t"
      :
      : [gdt] "r"(gdt_pointer),
        [code_selector] "r"(code_selector),
        [data_selector] "i"(kKernelDataSelector),
        [tss_selector] "i"(kKernelTssSelector)
      : "rax", "memory");
}

uint16_t read_task_register_selector() {
  uint16_t selector = 0;
  asm volatile("str %0" : "=r"(selector));
  return selector;
}

}  // namespace

bool initialize_tss() {
  if (g_tss_ready) {
    return true;
  }

  memory_set(&g_kernel_tss, 0, sizeof(g_kernel_tss));
  g_kernel_tss.rsp0 =
      stack_top_address(g_kernel_tss_rsp0_stack,
                        sizeof(g_kernel_tss_rsp0_stack));   // 以后 ring 3 -> ring 0 时，CPU 先切到这根内核栈。
  g_kernel_tss_default_rsp0 = g_kernel_tss.rsp0;
  g_kernel_tss.ist1 =
      stack_top_address(g_kernel_double_fault_ist_stack,
                        sizeof(g_kernel_double_fault_ist_stack));  // double fault 单独走应急栈，避免沿用可能已损坏的当前栈。

  // 如果 I/O bitmap 不准备使用，就把偏移设到 TSS 末尾之后；
  // 这样 CPU 会把它理解成“没有额外的 I/O 权限位图”。
  g_kernel_tss.io_map_base =
      static_cast<uint16_t>(sizeof(g_kernel_tss));

  build_kernel_gdt();

  GdtPointer gdt_pointer{};
  gdt_pointer.limit = static_cast<uint16_t>(sizeof(g_kernel_gdt) - 1);
  gdt_pointer.base = reinterpret_cast<uint64_t>(&g_kernel_gdt[0]);

  load_kernel_gdt_and_tss(&gdt_pointer);
  g_tss_ready = (read_task_register_selector() == kKernelTssSelector);
  return g_tss_ready;
}

bool tss_is_ready() {
  return g_tss_ready;
}

uint16_t tss_task_register_selector() {
  if (!g_tss_ready) {
    return 0;
  }

  return read_task_register_selector();
}

uint64_t tss_kernel_rsp0() {
  return g_kernel_tss.rsp0;
}

uint64_t tss_default_kernel_rsp0() {
  return g_kernel_tss_default_rsp0;
}

bool tss_set_kernel_rsp0(uint64_t rsp0) {
  if (!g_tss_ready || rsp0 == 0) {
    return false;
  }

  g_kernel_tss.rsp0 = rsp0;
  return true;
}

uint64_t tss_double_fault_ist1() {
  return g_kernel_tss.ist1;
}

uint16_t tss_io_map_base() {
  return g_kernel_tss.io_map_base;
}

bool initialize_idt() {
  memory_set(g_idt, 0, sizeof(g_idt));    // 先把所有槽位清零，避免脏数据进入 CPU。

  // 前 32 项先接 CPU 异常。
  for (uint8_t vector = 0; vector < kCpuExceptionCount; ++vector) {
    const uint8_t ist_index =
        (vector == kDoubleFaultVector) ? kKernelDoubleFaultIstIndex : 0;
    set_idt_gate(vector,
                 reinterpret_cast<void (*)()>(isr_stub_table[vector]),
                 kInterruptGateType,
                 ist_index);
  }

  // 再把 PIC 的 16 路硬件 IRQ 接到 32~47。
  for (uint8_t irq = 0; irq < kHardwareIrqCount; ++irq) {
    set_idt_gate(static_cast<uint8_t>(kPicMasterVectorBase + irq),
                 reinterpret_cast<void (*)()>(irq_stub_table[irq]));
  }

  // 第一版真正的 syscall 入口先走 `int 0x80`。
  // 这里先把门权限放成 DPL=3，这样以后真的有 ring 3 时可以继续沿用。
  set_idt_gate(kSyscallInterruptVector,
               syscall_interrupt_stub,
               kUserInterruptGateType);

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

void enable_interrupts() {
  asm volatile("sti");   // Set Interrupt Flag：从这一刻开始，CPU 才会接可屏蔽外部中断。
}

void disable_interrupts() {
  asm volatile("cli");   // Clear Interrupt Flag：后续即使 PIC 再发 IRQ，CPU 也先不接了。
}

bool interrupts_are_enabled() {
  uint64_t flags = 0;
  asm volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
  return (flags & (1ULL << 9)) != 0;
}

void wait_for_interrupt() {
  asm volatile("hlt");   // Halt：让 CPU 休眠，等下一次中断再把它唤醒。
}

extern "C" void kernel_handle_irq(const InterruptFrame* frame) {
  if (frame == nullptr) {
    return;
  }

  if (frame->vector == kPicMasterVectorBase) {
    handle_timer_irq();
  } else if (frame->vector == static_cast<uint8_t>(kPicMasterVectorBase + 1)) {
    handle_keyboard_irq();
  }

  // IRQ 收到以后，不管具体是哪一路，最后都要记得 EOI。
  send_pic_eoi(static_cast<uint8_t>(frame->vector));
}
