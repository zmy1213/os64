#include "perf/perf.hpp"
#include "cpu/cpu.hpp"
#include "interrupts/pit.hpp"
#include "log/log.hpp"
#include "memory/kmemory.hpp"
#include "task/scheduler.hpp"
#include "runtime/runtime.hpp"
void performance_snapshot(PerformanceSnapshot* output) {
  if(output==nullptr) return;
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  memory_set(output,0,sizeof(*output));
  output->abi_version=1;
  output->ticks=timer_tick_count();
  output->timer_hz=timer_frequency_hz();
  auto* const scheduler=scheduler_active_state();
  if(scheduler!=nullptr) {
    output->context_switches=scheduler->total_switches;
    output->yields=scheduler->total_yields;
    output->preempt_requests=scheduler->preempt_request_count;
    output->live_threads=scheduler->live_thread_count;
    output->ready_threads=scheduler->ready_count;
    output->blocked_threads=scheduler->blocked_thread_count;
    output->sleeping_threads=scheduler->sleeping_thread_count;
  }
  auto* allocator=kernel_memory_page_allocator();
  if(allocator!=nullptr) output->free_pages=count_free_pages(allocator);
  output->heap_used_bytes=heap_used_bytes(kernel_memory_heap());
  const auto log=kernel_log_stats();
  output->log_next_sequence=log.next_sequence;
  output->log_overwritten=log.overwritten;
  output->cpu_features=cpu_information().features;
  output->tsc=cpu_read_tsc(); // raw virtual cycles; deliberately not converted to nanoseconds.
  if(flags&(1ULL<<9)) asm volatile("sti" : : : "memory");
}
