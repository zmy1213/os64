#include "task/scheduler.hpp"

#include "interrupts/interrupts.hpp"
#include "memory/kmemory.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint8_t kInvalidReadySlot = 0xFF;
constexpr size_t kMinimumThreadStackBytes = 4096;

SchedulerState* g_active_scheduler = nullptr;  // 当前系统里先只保留 1 个全局活跃调度器。

extern "C" void scheduler_switch_context(uint64_t* saved_stack_pointer,
                                         uint64_t load_stack_pointer);
extern "C" void scheduler_thread_bootstrap();

uint64_t align_down(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  return value & ~(alignment - 1);
}

void copy_name(char* destination, const char* source) {
  if (destination == nullptr) {
    return;
  }

  if (source == nullptr || source[0] == '\0') {
    destination[0] = '\0';
    return;
  }

  size_t index = 0;
  while (index + 1 < kSchedulerNameCapacity && source[index] != '\0') {
    destination[index] = source[index];
    ++index;
  }
  destination[index] = '\0';
}

uint8_t thread_slot_index(const SchedulerState* scheduler,
                          const ThreadControlBlock* thread) {
  if (scheduler == nullptr || thread == nullptr) {
    return kInvalidReadySlot;
  }

  for (uint8_t i = 0; i < kSchedulerMaxThreadCount; ++i) {
    if (&scheduler->threads[i] == thread) {
      return i;
    }
  }

  return kInvalidReadySlot;
}

bool push_ready_thread(SchedulerState* scheduler,
                       ThreadControlBlock* thread) {
  if (scheduler == nullptr || thread == nullptr ||
      scheduler->ready_count >= kSchedulerMaxThreadCount) {
    return false;
  }

  const uint8_t slot = thread_slot_index(scheduler, thread);
  if (slot == kInvalidReadySlot) {
    return false;
  }

  scheduler->ready_queue_thread_slots[scheduler->ready_tail] = slot;
  scheduler->ready_tail =
      (scheduler->ready_tail + 1) % kSchedulerMaxThreadCount;
  ++scheduler->ready_count;
  return true;
}

ThreadControlBlock* pop_ready_thread(SchedulerState* scheduler) {
  if (scheduler == nullptr || scheduler->ready_count == 0) {
    return nullptr;
  }

  while (scheduler->ready_count > 0) {
    const uint8_t slot =
        scheduler->ready_queue_thread_slots[scheduler->ready_head];
    scheduler->ready_queue_thread_slots[scheduler->ready_head] =
        kInvalidReadySlot;
    scheduler->ready_head =
        (scheduler->ready_head + 1) % kSchedulerMaxThreadCount;
    --scheduler->ready_count;

    if (slot >= kSchedulerMaxThreadCount) {
      continue;
    }

    ThreadControlBlock* const thread = &scheduler->threads[slot];
    if (!thread->in_use || thread->state != kThreadStateReady) {
      continue;
    }

    return thread;
  }

  return nullptr;
}

ProcessControlBlock* first_free_process_slot(SchedulerState* scheduler) {
  if (scheduler == nullptr) {
    return nullptr;
  }

  for (size_t i = 0; i < kSchedulerMaxProcessCount; ++i) {
    if (!scheduler->processes[i].in_use) {
      return &scheduler->processes[i];
    }
  }

  return nullptr;
}

ThreadControlBlock* first_free_thread_slot(SchedulerState* scheduler) {
  if (scheduler == nullptr) {
    return nullptr;
  }

  for (size_t i = 0; i < kSchedulerMaxThreadCount; ++i) {
    if (!scheduler->threads[i].in_use) {
      return &scheduler->threads[i];
    }
  }

  return nullptr;
}

void prepare_initial_thread_stack(ThreadControlBlock* thread) {
  if (thread == nullptr || thread->stack_allocation == nullptr ||
      thread->stack_allocation_bytes < kMinimumThreadStackBytes) {
    return;
  }

  const uint64_t raw_stack_end =
      reinterpret_cast<uint64_t>(thread->stack_allocation) +
      thread->stack_allocation_bytes;

  // 线程第一次“被切进去”时，并不是通过真正的 `call` 进入入口函数，
  // 而是靠上下文切换里的 `ret` 落到 bootstrap。
  // 所以这里要手工伪造一份“像是已经 call 过”的最小栈帧。
  uint64_t* stack_cursor = reinterpret_cast<uint64_t*>(
      static_cast<uintptr_t>(align_down(raw_stack_end, 16) - 8));

  *--stack_cursor =
      reinterpret_cast<uint64_t>(&scheduler_thread_bootstrap);  // `ret` 之后第一次落到这里。
  *--stack_cursor = 0;                                          // rbp
  *--stack_cursor = 0;                                          // rbx
  *--stack_cursor = 0;                                          // r12
  *--stack_cursor = 0;                                          // r13
  *--stack_cursor = 0;                                          // r14
  *--stack_cursor = 0;                                          // r15

  thread->stack_top = raw_stack_end;
  thread->saved_stack_pointer =
      reinterpret_cast<uint64_t>(stack_cursor);
}

void mark_thread_running(SchedulerState* scheduler,
                         ThreadControlBlock* thread) {
  if (scheduler == nullptr || thread == nullptr) {
    return;
  }

  scheduler->current_thread = thread;
  scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  scheduler->preempt_requested = false;
  thread->state = kThreadStateRunning;
  ++thread->dispatch_count;

  if (thread->owner != nullptr) {
    thread->owner->state = kProcessStateRunning;
    ++thread->owner->dispatch_count;
  }
}

ThreadControlBlock* switch_to_next_ready_thread(
    SchedulerState* scheduler,
    ThreadControlBlock* current_thread) {
  if (scheduler == nullptr || current_thread == nullptr) {
    return nullptr;
  }

  ThreadControlBlock* const next_thread = pop_ready_thread(scheduler);
  if (next_thread == nullptr) {
    scheduler->preempt_requested = false;
    scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
    return nullptr;
  }

  mark_thread_running(scheduler, next_thread);
  ++scheduler->total_switches;
  scheduler_switch_context(&current_thread->saved_stack_pointer,
                           next_thread->saved_stack_pointer);
  return next_thread;
}

SchedulerState* active_scheduler() {
  return g_active_scheduler;
}

}  // namespace

bool initialize_scheduler(SchedulerState* scheduler,
                          uint32_t time_slice_ticks) {
  if (scheduler == nullptr || time_slice_ticks == 0) {
    return false;
  }

  memory_set(scheduler, 0, sizeof(*scheduler));
  scheduler->ready = true;
  scheduler->next_pid = 1;
  scheduler->next_tid = 1;
  scheduler->time_slice_ticks = time_slice_ticks;
  scheduler->remaining_slice_ticks = time_slice_ticks;

  for (size_t i = 0; i < kSchedulerMaxThreadCount; ++i) {
    scheduler->ready_queue_thread_slots[i] = kInvalidReadySlot;
  }

  g_active_scheduler = scheduler;
  return true;
}

bool scheduler_is_ready(const SchedulerState* scheduler) {
  return scheduler != nullptr && scheduler->ready;
}

ProcessControlBlock* scheduler_create_kernel_process(
    SchedulerState* scheduler,
    const char* name) {
  if (!scheduler_is_ready(scheduler)) {
    return nullptr;
  }

  ProcessControlBlock* const process = first_free_process_slot(scheduler);
  if (process == nullptr) {
    return nullptr;
  }

  memory_set(process, 0, sizeof(*process));
  process->in_use = true;
  process->is_kernel_process = true;
  process->pid = scheduler->next_pid++;
  process->state = kProcessStateReady;
  copy_name(process->name, name);
  return process;
}

ThreadControlBlock* scheduler_create_kernel_thread(
    SchedulerState* scheduler,
    ProcessControlBlock* owner,
    const char* name,
    KernelThreadEntry entry,
    void* entry_context,
    size_t stack_bytes) {
  if (!scheduler_is_ready(scheduler) || owner == nullptr || !owner->in_use ||
      entry == nullptr) {
    return nullptr;
  }

  ThreadControlBlock* const thread = first_free_thread_slot(scheduler);
  if (thread == nullptr) {
    return nullptr;
  }

  if (stack_bytes == 0) {
    stack_bytes = kSchedulerDefaultKernelThreadStackBytes;
  }
  if (stack_bytes < kMinimumThreadStackBytes) {
    stack_bytes = kMinimumThreadStackBytes;
  }

  void* const stack_allocation = kmalloc_aligned(stack_bytes, 16);
  if (stack_allocation == nullptr) {
    return nullptr;
  }

  memory_set(thread, 0, sizeof(*thread));
  thread->in_use = true;
  thread->tid = scheduler->next_tid++;
  thread->state = kThreadStateReady;
  thread->owner = owner;
  thread->entry = entry;
  thread->entry_context = entry_context;
  thread->stack_allocation = stack_allocation;
  thread->stack_allocation_bytes = stack_bytes;
  copy_name(thread->name, name);
  prepare_initial_thread_stack(thread);

  ++owner->live_thread_count;
  ++scheduler->live_thread_count;

  if (!push_ready_thread(scheduler, thread)) {
    thread->in_use = false;
    --owner->live_thread_count;
    --scheduler->live_thread_count;
    (void)kfree(stack_allocation);
    return nullptr;
  }

  return thread;
}

bool scheduler_run_until_idle(SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread != nullptr) {
    return false;
  }

  ThreadControlBlock* const next_thread = pop_ready_thread(scheduler);
  if (next_thread == nullptr) {
    return false;
  }

  mark_thread_running(scheduler, next_thread);
  ++scheduler->total_switches;
  scheduler_switch_context(&scheduler->bootstrap_stack_pointer,
                           next_thread->saved_stack_pointer);
  return scheduler->live_thread_count == 0;
}

bool scheduler_yield_current_thread() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr) {
    return false;
  }

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread->state != kThreadStateRunning) {
    return false;
  }

  if (scheduler->ready_count == 0) {
    scheduler->preempt_requested = false;
    scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
    return false;
  }

  current_thread->state = kThreadStateReady;
  ++current_thread->yield_count;
  ++scheduler->total_yields;

  if (current_thread->owner != nullptr &&
      current_thread->owner->state == kProcessStateRunning) {
    current_thread->owner->state = kProcessStateReady;
  }

  if (!push_ready_thread(scheduler, current_thread)) {
    current_thread->state = kThreadStateRunning;
    return false;
  }

  if (switch_to_next_ready_thread(scheduler, current_thread) == nullptr) {
    current_thread->state = kThreadStateRunning;
    scheduler->current_thread = current_thread;
    return false;
  }

  return true;
}

bool scheduler_yield_if_requested() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || !scheduler->preempt_requested ||
      scheduler->current_thread == nullptr) {
    return false;
  }

  return scheduler_yield_current_thread();
}

[[noreturn]] void scheduler_exit_current_thread() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr) {
    for (;;) {
      wait_for_interrupt();
    }
  }

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  current_thread->state = kThreadStateFinished;

  if (current_thread->owner != nullptr) {
    if (current_thread->owner->live_thread_count > 0) {
      --current_thread->owner->live_thread_count;
    }

    if (current_thread->owner->live_thread_count == 0) {
      current_thread->owner->state = kProcessStateExited;
    } else {
      current_thread->owner->state = kProcessStateReady;
    }
  }

  if (scheduler->live_thread_count > 0) {
    --scheduler->live_thread_count;
  }

  ThreadControlBlock* const next_thread = pop_ready_thread(scheduler);
  if (next_thread != nullptr) {
    mark_thread_running(scheduler, next_thread);
    ++scheduler->total_switches;
    scheduler_switch_context(&current_thread->saved_stack_pointer,
                             next_thread->saved_stack_pointer);
  }

  scheduler->current_thread = nullptr;
  scheduler->preempt_requested = false;
  scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  scheduler_switch_context(&current_thread->saved_stack_pointer,
                           scheduler->bootstrap_stack_pointer);

  for (;;) {
    wait_for_interrupt();
  }
}

void scheduler_handle_timer_tick() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    return;
  }

  ++scheduler->total_ticks;

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread == nullptr ||
      current_thread->state != kThreadStateRunning) {
    return;
  }

  ++current_thread->consumed_ticks;
  if (current_thread->owner != nullptr) {
    ++current_thread->owner->total_thread_ticks;
  }

  if (scheduler->time_slice_ticks == 0) {
    return;
  }

  if (scheduler->remaining_slice_ticks == 0) {
    scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  }

  --scheduler->remaining_slice_ticks;
  if (scheduler->remaining_slice_ticks != 0) {
    return;
  }

  scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  if (scheduler->ready_count == 0) {
    return;
  }

  scheduler->preempt_requested = true;
  ++scheduler->preempt_request_count;
}

ThreadControlBlock* scheduler_current_thread(
    const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return nullptr;
  }

  return scheduler->current_thread;
}

uint32_t scheduler_ready_thread_count(const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return 0;
  }

  return scheduler->ready_count;
}

uint32_t scheduler_live_thread_count(const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return 0;
  }

  return scheduler->live_thread_count;
}

const char* scheduler_process_state_name(ProcessState state) {
  switch (state) {
    case kProcessStateFree:
      return "free";
    case kProcessStateReady:
      return "ready";
    case kProcessStateRunning:
      return "running";
    case kProcessStateExited:
      return "exited";
    default:
      return "invalid";
  }
}

const char* scheduler_thread_state_name(ThreadState state) {
  switch (state) {
    case kThreadStateFree:
      return "free";
    case kThreadStateReady:
      return "ready";
    case kThreadStateRunning:
      return "running";
    case kThreadStateBlocked:
      return "blocked";
    case kThreadStateFinished:
      return "finished";
    default:
      return "invalid";
  }
}

extern "C" void scheduler_thread_bootstrap() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    for (;;) {
      wait_for_interrupt();
    }
  }

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread == nullptr || current_thread->entry == nullptr) {
    scheduler_exit_current_thread();
  }

  current_thread->entry(current_thread->entry_context);
  scheduler_exit_current_thread();
}
