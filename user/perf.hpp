#ifndef OS64_USER_PERF_HPP
#define OS64_USER_PERF_HPP
#include <stdint.h>
// Keep in sync with kernel/perf/perf.hpp and kernel/log/log.hpp.
struct PerformanceSnapshot {
  uint64_t abi_version, ticks, timer_hz, context_switches, yields, preempt_requests;
  uint64_t free_pages, heap_used_bytes, live_threads, ready_threads;
  uint64_t blocked_threads, sleeping_threads, log_next_sequence, log_overwritten;
  uint64_t cpu_features, tsc;
};
struct KernelLogRecord {
  uint64_t sequence, ticks;
  uint32_t level;
  char component[16];
  char message[88];
  uint32_t reserved;
};
static_assert(sizeof(PerformanceSnapshot)==128,"performance ABI");
static_assert(sizeof(KernelLogRecord)==128,"log ABI");
#endif
