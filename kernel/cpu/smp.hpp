#ifndef OS64_SMP_HPP
#define OS64_SMP_HPP
#include <stddef.h>
#include <stdint.h>
struct SchedulerState;
struct PageAllocator;
constexpr uint32_t kSmpMaxCpuCount = 4;
constexpr uint8_t kSmpTimerVector = 0xf0;
constexpr uint8_t kSmpRescheduleVector = 0xf1;
constexpr uint8_t kSmpSpuriousVector = 0xff;
// ABI v1: a CPU count is capacity; dispatch/tick counters show actual work.
struct SmpSnapshot {
  uint64_t abi_version;
  uint64_t online_cpus;
  uint64_t current_cpu;
  uint64_t online_mask;
  uint64_t user_dispatches[kSmpMaxCpuCount];
  uint64_t user_ticks[kSmpMaxCpuCount];
  uint64_t apic_ids[kSmpMaxCpuCount];
};
static_assert(sizeof(SmpSnapshot) == 128, "SMP snapshot ABI");
// BSP: interrupts must be enabled for PIT calibration/startup timeouts.
// false is fatal for runtime startup; no hotplug or partial online fallback.
bool smp_initialize(SchedulerState *scheduler, PageAllocator *allocator);
bool smp_is_enabled();
uint32_t smp_current_cpu_index();
uint32_t smp_online_cpu_count();
void smp_snapshot(SmpSnapshot *output);
void smp_send_reschedule(uint32_t target_cpu);
void smp_local_apic_eoi();
// CPU-owned gate, retained through saving old RSP/FX and context switches.
// Release only with IF=0 immediately before ring3 return or idle STI/HLT.
extern "C" void kernel_gate_enter();
extern "C" void kernel_gate_leave_user();
extern "C" void kernel_gate_release_idle();
#endif
