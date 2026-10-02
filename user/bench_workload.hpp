#ifndef OS64_BENCH_WORKLOAD_HPP
#define OS64_BENCH_WORKLOAD_HPP
#include <stdint.h>
constexpr uint64_t kBenchMultiplier=6364136223846793005ULL;
constexpr uint64_t kBenchIncrement=1442695040888963407ULL;
// The exact same dependency-heavy integer loop is compiled for both guests.
// Unsigned overflow is defined; the result cannot be optimized away.
__attribute__((noinline)) inline uint64_t bench_compute(uint64_t iterations, uint64_t state) {
  for(uint64_t i=0;i<iterations;++i) state=state*kBenchMultiplier+kBenchIncrement;
  return state;
}
// O(log N) affine jump-ahead independently verifies the loop without doubling
// the timed work. This also detects accidentally skipped or extra iterations.
inline uint64_t bench_expected(uint64_t iterations, uint64_t state) {
  uint64_t multiply=1, add=0, step_multiply=kBenchMultiplier, step_add=kBenchIncrement;
  while(iterations) {
    if(iterations&1) { multiply*=step_multiply; add=add*step_multiply+step_add; }
    step_add=(step_multiply+1)*step_add;
    step_multiply*=step_multiply;
    iterations>>=1;
  }
  return multiply*state+add;
}
inline uint32_t bench_checksum(uint64_t state) {
  // Linux exit status carries eight bits. Reserve 252..255 for worker errors;
  // each worker separately compares the complete 64-bit result before exiting.
  return static_cast<uint32_t>((state^(state>>32))%251);
}
#endif
