#ifndef OS64_USER_SMP_HPP
#define OS64_USER_SMP_HPP
#include "os64.hpp"
struct SmpSnapshot {
  uint64_t abi_version;
  uint64_t online_cpus;
  uint64_t current_cpu;
  uint64_t online_mask;
  uint64_t user_dispatches[4];
  uint64_t user_ticks[4];
  uint64_t apic_ids[4];
};
static_assert(sizeof(SmpSnapshot) == 128, "SMP snapshot ABI");
inline int64_t smp_snapshot(SmpSnapshot *output) {
  return syscall(41, reinterpret_cast<uint64_t>(output));
}
#endif
