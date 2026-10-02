#include "cpu/smp.hpp"
#include "cpu/cpu.hpp"
#include "cpu/topology.hpp"
#include "cpu/xapic.hpp"
#include "interrupts/interrupts.hpp"
#include "interrupts/pit.hpp"
#include "interrupts/serial.hpp"
#include "log/log.hpp"
#include "memory/kmemory.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"
#include "task/scheduler.hpp"

namespace {
// LAPIC 不是普通 RAM：用单独的高半区、supervisor + NX + PCD/PWT 映射。
// 初始化发生在创建正式用户页表之前；以后用户根共享内核高半区表。
constexpr uint64_t kLocalApicVirtual = 0xfffffe0000000000ULL;
constexpr uint32_t kNoOwner = 0xffffffff;
constexpr uint32_t kApicBaseMsr = 0x1b;
struct CpuStartup {
  uint32_t apic_id, online;
  void *stack;
};
CpuStartup g_cpus[kSmpMaxCpuCount]{};
CpuTopology g_topology{};
uint8_t g_cpu_lookup[256]{};
bool g_lookup_ready = false;
uint32_t g_gate_enabled = 0;
// 所有权属于 CPU，不属于线程：同一核心在持锁时可以切换内核栈。
// cache-line 对齐减少它与无关静态变量的伪共享；尚未实现公平排队锁。
alignas(64) uint32_t g_gate_owner = kNoOwner;
uint32_t g_online_count = 1;
uint32_t g_timer_count = 0;
uint64_t g_kernel_root = 0;
SchedulerState *g_scheduler = nullptr;
bool g_apic_ready = false;
extern "C" uint8_t smp_trampoline_start[], smp_trampoline_end[];
// BSP 每次只启动一个 AP。AP 报告 online 前读完邮箱，下一颗才可覆盖。
// 0x7000 的 trampoline 与 0x7e00 邮箱均在已经保留的低 1 MiB 内。
struct ApMailbox {
  uint32_t root, efer;
  uint64_t stack, entry;
  uint32_t index, reserved;
};
static_assert(sizeof(ApMailbox) == 32 && offsetof(ApMailbox, index) == 24,
              "AP mailbox assembly ABI");
uint8_t hardware_apic_id() {
  uint32_t a, b, c, d;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
  return uint8_t(b >> 24);
}
uint64_t read_msr(uint32_t msr) {
  uint32_t low, high;
  asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return (uint64_t(high) << 32) | low;
}
void write_msr(uint32_t msr, uint64_t value) {
  asm volatile("wrmsr"
               :
               : "c"(msr), "a"(uint32_t(value)), "d"(uint32_t(value >> 32))
               : "memory");
}
uint32_t apic_read(uint32_t offset) {
  return *reinterpret_cast<volatile uint32_t *>(kLocalApicVirtual + offset);
}
void apic_write(uint32_t offset, uint32_t value) {
  *reinterpret_cast<volatile uint32_t *>(kLocalApicVirtual + offset) = value;
  (void)apic_read(0x20); // flush the posted MMIO write.
}
bool local_apic_initialize(bool bsp) {
  const uint64_t base = read_msr(kApicBaseMsr);
  // 每颗 CPU 都先验证模式与完整基址，不能在错误的 MMIO 地址先写再检查。
  if (!xapic_base_supported(base, g_topology.local_apic_physical))
    return false;
  write_msr(kApicBaseMsr, base | (1ULL << 11));
  apic_write(0x80, 0); // accept all interrupt priorities.
  apic_write(0x320, 0x10000 | kSmpTimerVector); // timer initially masked.
  apic_write(0x350,
             bsp ? 0x700 : 0x10000); // BSP retains PIC virtual-wire ExtINT.
  apic_write(0x360, 0x10000);        // no unsupported external AP NMI routing.
  apic_write(0x370, 0x10000);        // mask error LVT until a handler exists.
  apic_write(0x280, 0);
  apic_write(0xf0, 0x100 | kSmpSpuriousVector);
  apic_write(0xb0, 0);
  return true;
}
bool wait_delivery() {
  for (uint32_t i = 0; i < 1000000; ++i) {
    if (!(apic_read(0x300) & (1U << 12)))
      return true;
    asm volatile("pause" : : : "memory");
  }
  return false;
}
bool send_ipi(uint32_t apic_id, uint32_t command) {
  if (!xapic_unicast_id_valid(apic_id) || !wait_delivery())
    return false;
  apic_write(0x310, apic_id << 24);
  apic_write(0x300, command);
  return wait_delivery();
}
void delay_tick() {
  const uint64_t begin = timer_tick_count();
  while (timer_tick_count() == begin)
    asm volatile("sti; hlt" : : : "memory");
}
const uint8_t *read_firmware(uint64_t physical, size_t size, void *) {
  const uint64_t limit = paging_managed_physical_limit();
  if (physical >= limit || size > limit - physical)
    return nullptr;
  return static_cast<const uint8_t *>(paging_physical_pointer(physical));
}
bool calibrate_timer() {
  apic_write(0x3e0, 3); // divide bus clock by 16.
  apic_write(0x380, 0xffffffff);
  const uint64_t begin = timer_tick_count();
  while (timer_tick_count() - begin < 5)
    asm volatile("sti; hlt" : : : "memory");
  const uint32_t delta = 0xffffffff - apic_read(0x390);
  const uint64_t ticks = timer_tick_count() - begin;
  apic_write(0x380, 0);
  if (!ticks || delta < ticks || ticks > 100)
    return false;
  g_timer_count = uint32_t(delta / ticks);
  return g_timer_count != 0;
}
[[noreturn]] void park_cpu() {
  for (;;)
    asm volatile("cli; hlt" : : : "memory");
}
extern "C" [[noreturn]] void smp_ap_entry(uint32_t index) {
  disable_interrupts();
  if (index == 0 || index >= g_topology.count ||
      hardware_apic_id() != g_cpus[index].apic_id || !cpu_initialize_local() ||
      !initialize_secondary_interrupts())
    park_cpu();
  if (!local_apic_initialize(false))
    park_cpu();
  apic_write(0x3e0, 3);
  apic_write(0x320,
             (1U << 17) | kSmpTimerVector); // periodic local scheduler timer.
  apic_write(0x380, g_timer_count);
  // 先更新总数，再 release 发布旗标；BSP acquire 看到 online 时总数必已更新。
  __atomic_fetch_add(&g_online_count, 1, __ATOMIC_ACQ_REL);
  __atomic_store_n(&g_cpus[index].online, 1, __ATOMIC_RELEASE);
  kernel_gate_enter();
  scheduler_run_secondary_cpu(g_scheduler, index);
}
} // namespace

// 初版用 CPUID 的 8 位 initial APIC ID 查表，避免依赖用户会重装的 FS/GS。
// 它是可移植的教学起点；频繁 CPUID 的成本将在性能报告中体现。
uint32_t smp_current_cpu_index() {
  if (!g_lookup_ready)
    return 0;
  const uint8_t slot = g_cpu_lookup[hardware_apic_id()];
  return slot < kSmpMaxCpuCount ? slot : 0;
}
bool smp_is_enabled() {
  return __atomic_load_n(&g_gate_enabled, __ATOMIC_ACQUIRE) != 0;
}
uint32_t smp_online_cpu_count() {
  return __atomic_load_n(&g_online_count, __ATOMIC_ACQUIRE);
}
// IRQ/syscall 汇编必须在访问任何共享内核状态前调用。等待锁时 IF=0，
// 不允许本 CPU 的嵌套 IRQ 在半入锁状态读调度器。原 IF 仅在取得后还原。
extern "C" void kernel_gate_enter() {
  if (!smp_is_enabled())
    return;
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  const uint32_t cpu = smp_current_cpu_index();
  for (;;) {
    uint32_t owner = __atomic_load_n(&g_gate_owner, __ATOMIC_ACQUIRE);
    if (owner == cpu)
      break; // a kernel IRQ is CPU-owned, not recursive.
    if (owner == kNoOwner &&
        __atomic_compare_exchange_n(&g_gate_owner, &owner, cpu, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      break;
    asm volatile("pause" : : : "memory");
  }
  if (flags & (1ULL << 9))
    enable_interrupts();
}
// 从用户陷入后所有内核 continuation 都保持持锁；最后一条 IRETQ 前
// 才释放。CLI 防止“锁已放开，CPU 却还在内核栈上被中断”的中间状态。
extern "C" void kernel_gate_leave_user() {
  if (!smp_is_enabled())
    return;
  disable_interrupts();
  if (__atomic_load_n(&g_gate_owner, __ATOMIC_RELAXED) ==
      smp_current_cpu_index())
    __atomic_store_n(&g_gate_owner, kNoOwner, __ATOMIC_RELEASE);
}
extern "C" void kernel_gate_release_idle() {
  disable_interrupts();
  kernel_gate_leave_user();
}
void smp_local_apic_eoi() {
  if (g_apic_ready)
    apic_write(0xb0, 0);
}
void smp_send_reschedule(uint32_t target_cpu) {
  if (!g_apic_ready || target_cpu >= g_topology.count ||
      !__atomic_load_n(&g_cpus[target_cpu].online, __ATOMIC_ACQUIRE) ||
      target_cpu == smp_current_cpu_index())
    return;
  (void)send_ipi(g_cpus[target_cpu].apic_id, kSmpRescheduleVector);
}
void smp_snapshot(SmpSnapshot *output) {
  if (!output)
    return;
  memory_set(output, 0, sizeof(*output));
  output->abi_version = 1;
  output->online_cpus = smp_online_cpu_count();
  output->current_cpu = smp_current_cpu_index();
  auto *scheduler = scheduler_active_state();
  for (uint32_t i = 0; i < kSmpMaxCpuCount; ++i) {
    const bool online =
        i == 0 || __atomic_load_n(&g_cpus[i].online, __ATOMIC_ACQUIRE);
    if (online)
      output->online_mask |= 1ULL << i;
    output->apic_ids[i] =
        i < g_topology.count ? g_cpus[i].apic_id : uint64_t(-1);
    if (scheduler) {
      output->user_dispatches[i] = scheduler->cpu_statistics[i].user_dispatches;
      output->user_ticks[i] = scheduler->cpu_statistics[i].user_ticks;
    }
  }
}
// 失败意味着停止正式 runtime，而不是在半在线状态继续分配线程。
// 全部 AP online 后返回时 BSP 仍持锁，首次调度按正常用户/idle 路径释放。
bool smp_initialize(SchedulerState *scheduler, PageAllocator *allocator) {
  if (!scheduler || !allocator || !interrupts_are_enabled() || smp_is_enabled())
    return false;
  g_topology = {};
  g_topology.count = 1;
  g_topology.apic_ids[0] = hardware_apic_id();
  g_cpus[0].apic_id = g_topology.apic_ids[0];
  g_cpus[0].online = 1;
  g_scheduler = scheduler;
  if (!scheduler_set_active(scheduler))
    return false;
  CpuTopology detected{};
  if (!firmware_find_cpu_topology(read_firmware, nullptr,
                                  uint8_t(g_cpus[0].apic_id), &detected)) {
    kernel_log_write(kLogWarning, "smp",
                     "No valid ACPI MADT/MP table; BSP only");
    return true;
  }
  g_topology = detected;
  if (g_topology.count == 1)
    return true;
  uint32_t a, b, c, d;
  asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
  if (!(d & (1U << 9)))
    return false;
  const uint64_t apic_base = read_msr(kApicBaseMsr);
  if (!xapic_base_supported(apic_base, g_topology.local_apic_physical))
    return false;
  g_kernel_root = paging_current_root_physical();
  if (!map_device_page(allocator, kLocalApicVirtual,
                       g_topology.local_apic_physical) ||
      !scheduler_prepare_smp(scheduler, g_topology.count))
    return false;
  memory_set(g_cpu_lookup, 0xff, sizeof(g_cpu_lookup));
  for (uint32_t i = 0; i < g_topology.count; ++i) {
    g_cpu_lookup[g_topology.apic_ids[i]] = uint8_t(i);
    g_cpus[i].apic_id = g_topology.apic_ids[i];
    if (i) {
      g_cpus[i].stack = kmalloc(32768);
      if (!g_cpus[i].stack)
        return false;
    }
  }
  g_lookup_ready = true;
  if (!local_apic_initialize(true))
    return false;
  g_apic_ready = true;
  if (!calibrate_timer())
    return false;
  const size_t bytes = size_t(smp_trampoline_end - smp_trampoline_start);
  if (!bytes || bytes > 0x800)
    return false;
  memory_copy(reinterpret_cast<void *>(0x7000), smp_trampoline_start, bytes);
  __atomic_store_n(&g_gate_enabled, 1, __ATOMIC_RELEASE);
  kernel_gate_enter();
  auto *mailbox = reinterpret_cast<volatile ApMailbox *>(0x7e00);
  for (uint32_t i = 1; i < g_topology.count; ++i) {
    mailbox->root = uint32_t(g_kernel_root);
    mailbox->efer = 0x100 | (paging_no_execute_enabled() ? 0x800 : 0);
    mailbox->stack = reinterpret_cast<uint64_t>(g_cpus[i].stack) + 32768;
    mailbox->entry = reinterpret_cast<uint64_t>(smp_ap_entry);
    mailbox->index = i;
    asm volatile("mfence" : : : "memory");
    if (!send_ipi(g_cpus[i].apic_id, 0xc500))
      return false;
    delay_tick();
    if (!send_ipi(g_cpus[i].apic_id, 0x8500) ||
        !send_ipi(g_cpus[i].apic_id, 0x600 | 7))
      return false;
    delay_tick();
    if (!__atomic_load_n(&g_cpus[i].online, __ATOMIC_ACQUIRE) &&
        !send_ipi(g_cpus[i].apic_id, 0x600 | 7))
      return false;
    const uint64_t begin = timer_tick_count();
    while (!__atomic_load_n(&g_cpus[i].online, __ATOMIC_ACQUIRE) &&
           timer_tick_count() - begin < 100)
      asm volatile("sti; hlt" : : : "memory");
    if (!__atomic_load_n(&g_cpus[i].online, __ATOMIC_ACQUIRE))
      return false;
  }
  kernel_log_write(kLogInfo, "smp",
                   "APs online; pinned users parallel, kernel serialized");
  return true;
}
