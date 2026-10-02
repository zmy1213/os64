#include "cpu/cpu.hpp"
#include "runtime/runtime.hpp"

namespace {
CpuInformation g_information{};
CpuFloatingState g_initial_state{};
bool g_initialized = false;
struct Registers { uint32_t a,b,c,d; };
Registers cpuid(uint32_t leaf, uint32_t subleaf=0) {
  Registers result;
  asm volatile("cpuid" : "=a"(result.a), "=b"(result.b),
               "=c"(result.c), "=d"(result.d) : "a"(leaf), "c"(subleaf));
  return result;
}
}

bool cpu_initialize() {
  if (g_initialized) return g_information.floating_state_enabled;
  const auto basic = cpuid(0);
  memory_copy(g_information.vendor, &basic.b, 4);
  memory_copy(g_information.vendor+4, &basic.d, 4);
  memory_copy(g_information.vendor+8, &basic.c, 4);
  const auto standard = cpuid(1);
  const uint32_t base_family = (standard.a >> 8) & 15;
  const uint32_t base_model = (standard.a >> 4) & 15;
  g_information.family = base_family + (base_family == 15 ? ((standard.a >> 20)&255) : 0);
  g_information.model = base_model | ((base_family == 6 || base_family == 15)
                                           ? ((standard.a >> 12)&240) : 0);
  g_information.stepping = standard.a & 15;
  if (standard.d & (1U<<0)) g_information.features |= kCpuFpu;
  if (standard.d & (1U<<24)) g_information.features |= kCpuFxsave;
  if (standard.d & (1U<<25)) g_information.features |= kCpuSse;
  if (standard.d & (1U<<26)) g_information.features |= kCpuSse2;
  if (standard.d & (1U<<4)) g_information.features |= kCpuTsc;
  const auto extended = cpuid(0x80000000);
  if (extended.a >= 0x80000001 && (cpuid(0x80000001).d & (1U<<20)))
    g_information.features |= kCpuNx;
  if (extended.a >= 0x80000007 && (cpuid(0x80000007).d & (1U<<8)))
    g_information.features |= kCpuInvariantTsc;
  if (extended.a >= 0x80000004) {
    for (uint32_t i=0; i<3; ++i) {
      const auto brand = cpuid(0x80000002+i);
      memory_copy(g_information.brand+i*16, &brand, 16);
    }
  }
  g_initialized = true;
  const uint64_t required = kCpuFpu | kCpuFxsave | kCpuSse | kCpuSse2;
  if ((g_information.features & required) != required) return false;
  uint64_t cr0, cr4;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~((1ULL<<2)|(1ULL<<3)); // EM=0, TS=0: eager FPU/SSE context switching.
  cr0 |= (1ULL<<1)|(1ULL<<5);    // MP=1, NE=1.
  asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  cr4 |= (1ULL<<9)|(1ULL<<10);   // OSFXSR and OSXMMEXCPT.
  asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
  const uint32_t mxcsr = 0x1f80;
  // A new thread must not inherit a previous thread's vector contents.
  // FNINIT marks x87 registers empty but does not erase their physical payload.
  // Fill all eight slots before resetting tags so the saved template has no
  // firmware or previous execution contents, even when inspected with FXSAVE.
  asm volatile("fninit\n\tfldz\n\tfldz\n\tfldz\n\tfldz\n\t"
               "fldz\n\tfldz\n\tfldz\n\tfldz\n\tfninit\n\tldmxcsr %0\n\t"
               "pxor %%xmm0,%%xmm0\n\tpxor %%xmm1,%%xmm1\n\t"
               "pxor %%xmm2,%%xmm2\n\tpxor %%xmm3,%%xmm3\n\t"
               "pxor %%xmm4,%%xmm4\n\tpxor %%xmm5,%%xmm5\n\t"
               "pxor %%xmm6,%%xmm6\n\tpxor %%xmm7,%%xmm7\n\t"
               "pxor %%xmm8,%%xmm8\n\tpxor %%xmm9,%%xmm9\n\t"
               "pxor %%xmm10,%%xmm10\n\tpxor %%xmm11,%%xmm11\n\t"
               "pxor %%xmm12,%%xmm12\n\tpxor %%xmm13,%%xmm13\n\t"
               "pxor %%xmm14,%%xmm14\n\tpxor %%xmm15,%%xmm15"
               : : "m"(mxcsr) : "memory");
  asm volatile("fxsave64 %0" : "=m"(g_initial_state) : : "memory");
  g_information.floating_state_enabled = true;
  return true;
}
const CpuInformation& cpu_information() { return g_information; }
void cpu_initialize_floating_state(CpuFloatingState* state) {
  if (state != nullptr) memory_copy(state, &g_initial_state, sizeof(*state));
}
uint64_t cpu_read_tsc() {
  if ((g_information.features & kCpuTsc) == 0) return 0;
  uint32_t low, high;
  asm volatile("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
  return (static_cast<uint64_t>(high)<<32)|low;
}
