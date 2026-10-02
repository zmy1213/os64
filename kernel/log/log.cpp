#include "log/log.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"

namespace {
KernelLogRecord g_records[kKernelLogCapacity]{};
uint64_t g_next_sequence=1, g_overwritten=0;
size_t g_count=0;
class InterruptGuard {
#if defined(OS64_LOG_HOST_TEST)
public:
  InterruptGuard() = default;
#else
  uint64_t flags_;
public:
  InterruptGuard() { asm volatile("pushfq; popq %0; cli" : "=r"(flags_) : : "memory"); }
  ~InterruptGuard() { if (flags_&(1ULL<<9)) asm volatile("sti" : : : "memory"); }
#endif
};
void copy_text(char* output, size_t capacity, const char* text) {
  size_t i=0;
  if (text != nullptr) for (; i+1<capacity && text[i]; ++i) output[i]=text[i];
  output[i]='\0';
}
}
void kernel_log_initialize() {
  InterruptGuard guard;
  memory_set(g_records,0,sizeof(g_records));
  g_next_sequence=1; g_overwritten=0; g_count=0;
}
void kernel_log_write(KernelLogLevel level, const char* component, const char* message) {
  InterruptGuard guard;
  KernelLogRecord& record=g_records[(g_next_sequence-1)%kKernelLogCapacity];
  memory_set(&record,0,sizeof(record));
  record.sequence=g_next_sequence++;
  record.ticks=timer_tick_count();
  record.level=static_cast<uint32_t>(level);
  copy_text(record.component,sizeof(record.component),component);
  copy_text(record.message,sizeof(record.message),message);
  if (g_count<kKernelLogCapacity) ++g_count;
  else ++g_overwritten;
}
void kernel_log_process_event(KernelLogLevel level, const char* event, uint32_t pid) {
  char message[88] = {}; size_t length = 0;
  if (event) { while (length < 65 && event[length]) { message[length] = event[length]; ++length; } }
  const char* suffix = " pid=";
  while (*suffix) { message[length++] = *suffix++; }
  char digits[10]; size_t count = 0;
  do { digits[count++] = static_cast<char>('0' + pid % 10); pid /= 10; } while (pid);
  while (count) { message[length++] = digits[--count]; }
  kernel_log_write(level, "process", message);
}
size_t kernel_log_read(uint64_t after_sequence, KernelLogRecord* output, size_t capacity) {
  if (output==nullptr || capacity==0) return 0;
  InterruptGuard guard;
  const uint64_t first=g_next_sequence-g_count;
  if (after_sequence>=g_next_sequence-1) return 0;
  uint64_t sequence=after_sequence<first ? first : after_sequence+1;
  size_t copied=0;
  for (; sequence<g_next_sequence && copied<capacity; ++sequence)
    memory_copy(&output[copied++],&g_records[(sequence-1)%kKernelLogCapacity],sizeof(*output));
  return copied;
}
KernelLogStats kernel_log_stats() {
  InterruptGuard guard;
  return {g_next_sequence,g_overwritten,g_count};
}
const char* kernel_log_level_name(uint32_t level) {
  switch(level) { case kLogDebug:return "debug"; case kLogInfo:return "info";
    case kLogWarning:return "warn"; case kLogError:return "error"; default:return "unknown"; }
}
