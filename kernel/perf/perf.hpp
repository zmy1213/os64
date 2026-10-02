#ifndef OS64_PERF_HPP
#define OS64_PERF_HPP
#include <stdint.h>
// All fields are uint64_t, ABI v1. Runtime callers hold the kernel gate;
// performance_snapshot also preserves IF while guarding local interrupts.
struct PerformanceSnapshot {
  uint64_t abi_version;
  uint64_t ticks;
  uint64_t timer_hz;
  uint64_t context_switches;
  uint64_t yields;
  uint64_t preempt_requests;
  uint64_t free_pages;
  uint64_t heap_used_bytes;
  uint64_t live_threads;
  uint64_t ready_threads;
  uint64_t blocked_threads;
  uint64_t sleeping_threads;
  uint64_t log_next_sequence;
  uint64_t log_overwritten;
  uint64_t cpu_features;
  uint64_t tsc;
};
static_assert(sizeof(PerformanceSnapshot)==128,"performance syscall ABI");
void performance_snapshot(PerformanceSnapshot* output);
#endif
