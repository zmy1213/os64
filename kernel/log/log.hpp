#ifndef OS64_LOG_HPP
#define OS64_LOG_HPP
#include <stddef.h>
#include <stdint.h>

enum KernelLogLevel : uint32_t { kLogDebug=0, kLogInfo=1, kLogWarning=2, kLogError=3 };
constexpr size_t kKernelLogCapacity = 256;
// Stable syscall ABI: fixed-size, no pointers, no kernel address disclosures.
struct KernelLogRecord {
  uint64_t sequence;
  uint64_t ticks;
  uint32_t level;
  char component[16];
  char message[88];
  uint32_t reserved;
};
static_assert(sizeof(KernelLogRecord)==128, "log syscall ABI");
struct KernelLogStats { uint64_t next_sequence, overwritten, count; };

// Single CPU: bounded CLI critical sections preserve the caller's IF.
// The record writer never calls serial, allocates, blocks or formats numbers.
void kernel_log_initialize();
void kernel_log_write(KernelLogLevel level, const char* component, const char* message);
// Bounded stack-only process message; called at lifecycle events, not per tick.
void kernel_log_process_event(KernelLogLevel level, const char* event, uint32_t pid);
size_t kernel_log_read(uint64_t after_sequence, KernelLogRecord* output, size_t capacity);
KernelLogStats kernel_log_stats();
const char* kernel_log_level_name(uint32_t level);
#endif
