#ifndef OS64_CPU_HPP
#define OS64_CPU_HPP
#include <stdint.h>

enum CpuFeature : uint64_t {
  kCpuFpu = 1ULL << 0, kCpuFxsave = 1ULL << 1,
  kCpuSse = 1ULL << 2, kCpuSse2 = 1ULL << 3,
  kCpuTsc = 1ULL << 4, kCpuNx = 1ULL << 5,
  kCpuInvariantTsc = 1ULL << 6,
};
struct CpuInformation {
  char vendor[13];
  char brand[49];
  uint32_t family, model, stepping;
  uint64_t features;
  bool floating_state_enabled;
};
// FXSAVE requires a 16-byte aligned 512-byte image, including x87 and XMM0–15.
// AVX upper halves are deliberately unsupported: CR4.OSXSAVE is not enabled.
struct alignas(16) CpuFloatingState { uint8_t bytes[512]; };
static_assert(sizeof(CpuFloatingState) == 512, "FXSAVE layout");

bool cpu_initialize();
// CR0/CR4 and floating registers belong to each CPU, even after global CPUID init.
bool cpu_initialize_local();
const CpuInformation& cpu_information();
void cpu_initialize_floating_state(CpuFloatingState* state);
uint64_t cpu_read_tsc();
#endif
