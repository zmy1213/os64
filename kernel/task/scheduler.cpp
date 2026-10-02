#include "task/scheduler.hpp"
#include "log/log.hpp"
#include "net/network.hpp"

#include "boot/segments.hpp"
#include "interrupts/interrupts.hpp"
#include "memory/kmemory.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint8_t kInvalidReadySlot = 0xFF;
constexpr size_t kMinimumThreadStackBytes = 4096;
constexpr size_t kUserKernelStackBytes = 32768;

SchedulerState* g_active_scheduler = nullptr;  // 当前系统里先只保留 1 个全局活跃调度器。

extern "C" void scheduler_switch_context_and_root(
    uint64_t* saved_stack_pointer,
    uint64_t load_stack_pointer,
    uint64_t load_root_physical,
    CpuFloatingState* save_floating_state,
    const CpuFloatingState* restore_floating_state);
extern "C" void scheduler_thread_bootstrap();
void reap_orphans(SchedulerState* scheduler);

// 调度器里的这批 helper，大多都在服务“线程切换前后，栈和地址空间根要怎么保存/恢复”。
uint64_t align_down(uint64_t value, uint64_t alignment) {
  if (alignment == 0) {
    return value;
  }

  return value & ~(alignment - 1);
}

void copy_name(char* destination, const char* source) {
  // 调度器里 process/thread 的名字只用于日志和观察，
  // 所以这里做一个最小安全拷贝，保证一定以 `\0` 结尾。
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
      // ready queue 里存的是“槽位号”，不是裸指针，
      // 所以这里负责把真实 TCB 指针反查成固定数组下标。
      return i;
    }
  }

  return kInvalidReadySlot;
}

bool is_runnable_priority(ThreadPriority priority) {
  return priority == kThreadPriorityHigh ||
         priority == kThreadPriorityNormal ||
         priority == kThreadPriorityBackground;
}

bool is_user_thread_ready_to_enter(const ThreadControlBlock* thread) {
  return thread != nullptr &&
         thread->execution_mode == kThreadExecutionModeUser &&
         thread->owner != nullptr &&
         thread->owner->address_space.ready &&
         thread->owner->address_space.root_physical_address != 0 &&
         thread->user_mode.user_instruction_pointer != 0 &&
         thread->user_mode.user_stack_pointer != 0 &&
         thread->user_mode.user_code_selector != 0 &&
         thread->user_mode.user_stack_selector != 0;
}

uint64_t scheduler_kernel_root_physical(const SchedulerState* scheduler) {
  // 调度器视角里的“kernel root”通常就是 0 号内核进程那份地址空间根。
  // 这样线程切回 bootstrap 或普通 kernel thread 时，就知道该恢复哪份 CR3。
  if (scheduler != nullptr &&
      scheduler->processes[0].in_use &&
      scheduler->processes[0].address_space.ready &&
      scheduler->processes[0].address_space.root_physical_address != 0) {
    return scheduler->processes[0].address_space.root_physical_address;
  }

  return paging_current_root_physical();
}

uint64_t thread_resume_root_physical(const SchedulerState* scheduler,
                                     const ThreadControlBlock* thread) {
  if (thread != nullptr && thread->saved_address_space_root_physical != 0) {
    return thread->saved_address_space_root_physical;
  }

  // 对还没真正跑起来过的新线程，默认先在 kernel root 下恢复它的初始内核上下文。
  // 这样：
  // - 普通 kernel thread 能直接安全使用 kernel heap 栈
  // - user thread 也会先在 kernel root 下跑 bootstrap，再由 `user_mode_enter()` 自己切去 user root
  return scheduler_kernel_root_physical(scheduler);
}

void initialize_thread_saved_root(SchedulerState* scheduler,
                                  ThreadControlBlock* thread) {
  if (thread == nullptr) {
    return;
  }

  thread->saved_address_space_root_physical =
      scheduler_kernel_root_physical(scheduler);
}

bool push_ready_thread(SchedulerState* scheduler,
                       ThreadControlBlock* thread) {
  if (scheduler == nullptr || thread == nullptr ||
      thread->is_idle_thread ||
      !is_runnable_priority(thread->priority) ||
      scheduler->ready_count >= (kSchedulerMaxThreadCount - 1)) {
    return false;
  }

  // ready queue 里存的不是裸指针，而是线程槽位编号。
  // 这样固定数组实现更简单，也更方便做一致性检查。
  const uint8_t slot = thread_slot_index(scheduler, thread);
  if (slot == kInvalidReadySlot) {
    return false;
  }

  const uint8_t priority = static_cast<uint8_t>(thread->priority);
  scheduler->ready_queue_thread_slots[priority][scheduler->ready_tail[priority]] =
      slot;
  scheduler->ready_tail[priority] =
      (scheduler->ready_tail[priority] + 1) % kSchedulerMaxThreadCount;
  ++scheduler->ready_count_by_priority[priority];
  ++scheduler->ready_count;
  return true;
}

ThreadControlBlock* pop_ready_thread_from_priority(
    SchedulerState* scheduler,
    ThreadPriority priority) {
  if (scheduler == nullptr || !is_runnable_priority(priority)) {
    return nullptr;
  }

  const uint8_t priority_index = static_cast<uint8_t>(priority);
  // 这里即使队列里有历史残留槽位，也会在真正取出时再做一轮状态过滤，
  // 只有“槽位有效 + 线程仍是 ready + 优先级匹配”的线程才会被选中。
  while (scheduler->ready_count_by_priority[priority_index] > 0) {
    const uint8_t slot =
        scheduler->ready_queue_thread_slots[priority_index]
                                         [scheduler->ready_head[priority_index]];
    scheduler->ready_queue_thread_slots[priority_index]
                                       [scheduler->ready_head[priority_index]] =
        kInvalidReadySlot;
    scheduler->ready_head[priority_index] =
        (scheduler->ready_head[priority_index] + 1) % kSchedulerMaxThreadCount;
    --scheduler->ready_count_by_priority[priority_index];
    --scheduler->ready_count;

    if (slot >= kSchedulerMaxThreadCount) {
      continue;
    }

    ThreadControlBlock* const thread = &scheduler->threads[slot];
    if (!thread->in_use ||
        thread->is_idle_thread ||
        thread->state != kThreadStateReady ||
        thread->priority != priority) {
      continue;
    }

    return thread;
  }

  return nullptr;
}

ThreadControlBlock* pop_highest_ready_thread(SchedulerState* scheduler) {
  if (scheduler == nullptr || scheduler->ready_count == 0) {
    return nullptr;
  }

  for (uint8_t priority = static_cast<uint8_t>(kThreadPriorityHigh);
       priority <= static_cast<uint8_t>(kThreadPriorityBackground);
       ++priority) {
    ThreadControlBlock* const thread =
        pop_ready_thread_from_priority(scheduler,
                                       static_cast<ThreadPriority>(priority));
    if (thread != nullptr) {
      return thread;
    }
  }

  return nullptr;
}

ProcessControlBlock* first_free_process_slot(SchedulerState* scheduler) {
  if (scheduler == nullptr) {
    return nullptr;
  }

  for (size_t i = 1; i < kSchedulerMaxProcessCount; ++i) {
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

  for (size_t i = 1; i < kSchedulerMaxThreadCount; ++i) {
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
  *--stack_cursor = 0x202;                                      // RFLAGS: new kernel contexts accept IRQs.
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

  // 以后如果这条线程此刻正准备跑在 ring 3，
  // 下一次从用户态打进内核时，CPU 就必须先切到“它自己那根专用内核进入栈”。
  //
  // 注意这里不能再复用 user thread 启动时那根 scheduler/bootstrap 栈。
  // 因为 `user_mode_enter()` 把“最后要退回哪条内核栈”的返回现场也压在那根栈上；
  // 如果每次 syscall 都继续把 trap frame 压到同一页顶端，
  // 最终 `exit` 时那份最初保存的返回现场就会被踩坏。
  //
  // 所以 user thread 现在明确拆成两根栈：
  // - `stack_top`：给 scheduler/bootstrap + `user_mode_enter()` 最终返回
  // - `user_kernel_entry_stack_top`：专门给 TSS.rsp0 接 ring 3 -> ring 0 的入口现场
  //
  // 对普通 kernel thread 则继续回退到初始化 TSS 时那根默认 RSP0。
  if (thread->execution_mode == kThreadExecutionModeUser &&
      thread->user_kernel_entry_stack_top != 0) {
    (void)tss_set_kernel_rsp0(thread->user_kernel_entry_stack_top);
  } else {
    (void)tss_set_kernel_rsp0(tss_default_kernel_rsp0());
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

void switch_thread_context(SchedulerState* scheduler,
                           ThreadControlBlock* current_thread,
                           ThreadControlBlock* next_thread) {
  if (scheduler == nullptr ||
      current_thread == nullptr ||
      next_thread == nullptr) {
    return;
  }

  // 这一步的核心就是：
  // 当前线程暂停下来的内核栈，依赖的是“此刻正在用的这份 CR3”；
  // 所以先把当前根页表也一起记下来。
  current_thread->saved_address_space_root_physical =
      paging_current_root_physical();

  const uint64_t next_root =
      thread_resume_root_physical(scheduler, next_thread);
  mark_thread_running(scheduler, next_thread);
  ++scheduler->total_switches;
  scheduler_switch_context_and_root(&current_thread->saved_stack_pointer,
                                    next_thread->saved_stack_pointer,
                                    next_root,
                                    &current_thread->floating_state,
                                    &next_thread->floating_state);
  reap_orphans(scheduler);
}

void switch_from_bootstrap_to_thread(SchedulerState* scheduler,
                                     ThreadControlBlock* next_thread) {
  if (scheduler == nullptr || next_thread == nullptr) {
    return;
  }

  const uint64_t next_root =
      thread_resume_root_physical(scheduler, next_thread);
  mark_thread_running(scheduler, next_thread);
  ++scheduler->total_switches;
  scheduler_switch_context_and_root(&scheduler->bootstrap_stack_pointer,
                                    next_thread->saved_stack_pointer,
                                    next_root,
                                    &scheduler->bootstrap_floating_state,
                                    &next_thread->floating_state);
}

void switch_from_thread_to_bootstrap(SchedulerState* scheduler,
                                     ThreadControlBlock* current_thread) {
  if (scheduler == nullptr || current_thread == nullptr) {
    return;
  }

  current_thread->saved_address_space_root_physical =
      paging_current_root_physical();
  scheduler_switch_context_and_root(&current_thread->saved_stack_pointer,
                                    scheduler->bootstrap_stack_pointer,
                                    scheduler_kernel_root_physical(scheduler),
                                    &current_thread->floating_state,
                                    &scheduler->bootstrap_floating_state);
}

void update_owner_ready_state(ProcessControlBlock* owner) {
  if (owner == nullptr) {
    return;
  }

  // 线程让出 CPU / sleep / block 以后，所属进程也应该从 running 回到 ready。
  if (owner->state == kProcessStateRunning) {
    owner->state = kProcessStateReady;
  }
}

ThreadControlBlock* select_next_runnable_thread(SchedulerState* scheduler) {
  if (scheduler == nullptr) {
    return nullptr;
  }

  // 选线程顺序很简单：
  // 1. 先按优先级从 ready queue 找普通线程
  // 2. 如果一个普通线程都没有，但系统里还有活线程，就退回 idle thread
  ThreadControlBlock* const next_thread = pop_highest_ready_thread(scheduler);
  if (next_thread != nullptr) {
    return next_thread;
  }

  if (scheduler->idle_thread != nullptr && scheduler->live_thread_count > 0) {
    return scheduler->idle_thread;
  }

  return nullptr;
}

bool wake_thread_internal(SchedulerState* scheduler,
                          ThreadControlBlock* thread,
                          bool request_reschedule_if_idle) {
  if (scheduler == nullptr || thread == nullptr || !thread->in_use ||
      thread->is_idle_thread) {
    return false;
  }

  if (thread->state == kThreadStateSleeping) {
    if (scheduler->sleeping_thread_count > 0) {
      --scheduler->sleeping_thread_count;
    }
  } else if (thread->state == kThreadStateBlocked) {
    if (scheduler->blocked_thread_count > 0) {
      --scheduler->blocked_thread_count;
    }
  } else {
    return false;
  }

  thread->state = kThreadStateReady;
  thread->wake_tick = 0;

  if (thread->owner != nullptr &&
      thread->owner->state != kProcessStateRunning &&
      thread->owner->state != kProcessStateExited) {
    thread->owner->state = kProcessStateReady;
  }

  if (!push_ready_thread(scheduler, thread)) {
    return false;
  }

  if (request_reschedule_if_idle &&
      scheduler->current_thread == scheduler->idle_thread &&
      !scheduler->preempt_requested) {
    scheduler->preempt_requested = true;
    ++scheduler->preempt_request_count;
  }

  return true;
}

void wake_sleeping_threads(SchedulerState* scheduler) {
  if (scheduler == nullptr || scheduler->sleeping_thread_count == 0) {
    return;
  }

  for (size_t i = 1; i < kSchedulerMaxThreadCount; ++i) {
    ThreadControlBlock* const thread = &scheduler->threads[i];
    if (!thread->in_use || thread->state != kThreadStateSleeping) {
      continue;
    }

    if (thread->wake_tick != 0 && thread->wake_tick <= scheduler->total_ticks) {
      (void)wake_thread_internal(scheduler, thread, true);
    }
  }
}

void idle_thread_entry(void*) {
  // idle thread 不做业务逻辑，它的意义只是：
  // 当没有普通线程能跑时，让 CPU 进入“等中断 + 看看是否该切换”的保底循环。
  for (;;) {
    wait_for_interrupt();
    reap_orphans(g_active_scheduler);
    (void)scheduler_yield_if_requested();
  }
}

SchedulerState* active_scheduler() {
  return g_active_scheduler;
}

void release_thread(ThreadControlBlock* thread) {
  if (thread->execution_mode == kThreadExecutionModeUser) {
    (void)kfree(thread->stack_allocation);
    (void)kfree(thread->user_kernel_entry_stack_allocation);
  } else if (thread->stack_allocation != nullptr) {
    (void)kfree(thread->stack_allocation);
  }
  memory_set(thread, 0, sizeof(*thread));
}

void release_process(ProcessControlBlock* process) {
  fd_close_all(&process->file_descriptors);
  if (!process->is_kernel_process && process->pid) network_udp_close_owner(process->pid);
  if (process->address_space.owns_page_table_root) {
    (void)address_space_destroy(&process->address_space, process->page_allocator);
  }
  memory_set(process, 0, sizeof(*process));
}

void reap_orphans(SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return;
  }
  for (auto& process : scheduler->processes) {
    if (process.in_use && process.auto_reap && process.state == kProcessStateExited &&
        (scheduler->current_thread == nullptr ||
         scheduler->current_thread->owner != &process)) {
      (void)scheduler_reap_process(scheduler, process.pid, nullptr);
    }
  }
}

}  // namespace

SchedulerState* scheduler_active_state() {
  return g_active_scheduler;
}

ProcessControlBlock* scheduler_find_process(SchedulerState* scheduler, uint32_t pid) {
  if (!scheduler_is_ready(scheduler)) {
    return nullptr;
  }
  for (auto& process : scheduler->processes) {
    if (process.in_use && process.pid == pid) {
      return &process;
    }
  }
  return nullptr;
}

bool scheduler_discard_process(SchedulerState* scheduler, uint32_t pid) {
  ProcessControlBlock* const process = scheduler_find_process(scheduler, pid);
  if (process == nullptr || pid == 0 ||
      (scheduler->current_thread != nullptr && scheduler->current_thread->owner == process)) {
    return false;
  }
  for (const auto& thread : scheduler->threads) {
    if (thread.in_use && thread.owner == process &&
        thread.state != kThreadStateFinished &&
        (thread.state != kThreadStateReady || thread.dispatch_count != 0)) {
      return false;
    }
  }
  for (size_t priority = 0; priority < kSchedulerPriorityCount; ++priority) {
    const uint32_t count = scheduler->ready_count_by_priority[priority];
    for (uint32_t i = 0; i < count; ++i) {
      ThreadControlBlock* const thread = pop_ready_thread_from_priority(
          scheduler, static_cast<ThreadPriority>(priority));
      if (thread != nullptr && thread->owner != process) {
        (void)push_ready_thread(scheduler, thread);
      }
    }
  }
  for (auto& thread : scheduler->threads) {
    if (thread.in_use && thread.owner == process) {
      if (thread.state != kThreadStateFinished && scheduler->live_thread_count != 0) {
        --scheduler->live_thread_count;
      }
      release_thread(&thread);
    }
  }
  release_process(process);
  return true;
}

bool scheduler_reap_process(SchedulerState* scheduler, uint32_t pid,
                             int64_t* exit_status) {
  ProcessControlBlock* const process = scheduler_find_process(scheduler, pid);
  if (process == nullptr || process->state != kProcessStateExited ||
      process->live_thread_count != 0) {
    return false;
  }
  const int64_t status = process->exit_code;
  if (!scheduler_discard_process(scheduler, pid)) {
    return false;
  }
  if (exit_status != nullptr) {
    *exit_status = status;
  }
  return true;
}

bool scheduler_destroy(SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread != nullptr ||
      scheduler->live_thread_count != 0 || scheduler->sleeping_thread_count != 0 ||
      scheduler->blocked_thread_count != 0) {
    return false;
  }
  for (auto& thread : scheduler->threads) {
    if (thread.in_use) {
      release_thread(&thread);
    }
  }
  for (auto& process : scheduler->processes) {
    if (process.in_use) {
      release_process(&process);
    }
  }
  if (g_active_scheduler == scheduler) {
    g_active_scheduler = nullptr;
    (void)tss_set_kernel_rsp0(tss_default_kernel_rsp0());
  }
  memory_set(scheduler, 0, sizeof(*scheduler));
  return true;
}

bool scheduler_prepare_user_arguments(ProcessControlBlock* process,
                                       ThreadControlBlock* thread,
                                       size_t argc, const char* const* argv) {
  constexpr size_t kMaxArguments = 16;
  constexpr size_t kMaxArgumentBytes = 256;
  if (process == nullptr || thread == nullptr || !process->in_use ||
      thread->owner != process || thread->state != kThreadStateReady ||
      thread->dispatch_count != 0 || thread->execution_mode != kThreadExecutionModeUser ||
      argc > kMaxArguments || (argc != 0 && argv == nullptr)) {
    return false;
  }
  const uint64_t top = thread->user_mode.user_stack_pointer;
  const uint64_t bottom = process->user_heap_base != 0
      ? kUserStackBottom : align_down(top - 1, kPagingPageSize);
  uint64_t pointers[kMaxArguments + 1];
  size_t lengths[kMaxArguments];
  memory_set(pointers, 0, sizeof(pointers));
  memory_set(lengths, 0, sizeof(lengths));
  size_t string_bytes = 0;
  for (size_t i = 0; i < argc; ++i) {
    if (argv[i] == nullptr) {
      return false;
    }
    size_t length = 0;
    while (length < kMaxArgumentBytes && argv[i][length] != '\0') {
      ++length;
    }
    if (length == kMaxArgumentBytes) {
      return false;
    }
    lengths[i] = length + 1;
    string_bytes += length + 1;
  }
  const uint64_t table_bytes = (argc + 2) * sizeof(uint64_t);
  if (string_bytes > top - bottom || table_bytes + 15 > top - bottom - string_bytes) {
    return false;
  }
  uint64_t cursor = top;
  for (size_t i = argc; i != 0; --i) {
    cursor -= lengths[i - 1];
    pointers[i - 1] = cursor;
    if (!address_space_copy_to_user(&process->address_space, cursor,
                                     argv[i - 1], lengths[i - 1])) {
      return false;
    }
  }
  const uint64_t stack = align_down(cursor - table_bytes, 16);
  const uint64_t count = argc;
  if (!address_space_copy_to_user(&process->address_space, stack, &count, sizeof(count)) ||
      !address_space_copy_to_user(&process->address_space, stack + sizeof(count),
                                   pointers, (argc + 1) * sizeof(uint64_t))) {
    return false;
  }
  thread->user_mode.user_stack_pointer = stack;
  return true;
}

bool scheduler_user_range_valid(uint64_t address, size_t bytes, bool writable) {
  ThreadControlBlock* const thread = scheduler_active_thread();
  return thread != nullptr && thread->owner != nullptr &&
         thread->execution_mode == kThreadExecutionModeUser &&
         address_space_user_range_valid(&thread->owner->address_space,
                                          address, bytes, writable);
}

bool scheduler_wait_process(SchedulerState* scheduler, uint32_t pid,
                             int64_t* exit_status) {
  if (scheduler != g_active_scheduler) {
    return false;
  }
  ProcessControlBlock* const process = scheduler_find_process(scheduler, pid);
  ThreadControlBlock* const waiter = scheduler_active_thread();
  if (process == nullptr || pid == 0 ||
      (waiter != nullptr && (waiter->owner == process ||
                            process->parent_pid != waiter->owner->pid))) {
    return false;
  }
  const bool restore_interrupts = interrupts_are_enabled();
  while (process->state != kProcessStateExited) {
    if (waiter == nullptr) {
      if (!scheduler_run_until_idle(scheduler)) {
        return false;
      }
    } else {
      disable_interrupts();
      if (process->state != kProcessStateExited) {
        waiter->waiting_for_pid = pid;
        if (!scheduler_block_current_thread_and_enable_interrupts()) {
          waiter->waiting_for_pid = 0;
          if (restore_interrupts) {
            enable_interrupts();
          }
          return false;
        }
        waiter->waiting_for_pid = 0;
      }
      if (restore_interrupts) {
        enable_interrupts();
      } else {
        disable_interrupts();
      }
    }
  }
  if (exit_status != nullptr) {
    *exit_status = process->exit_code;
  }
  return true;
}

[[noreturn]] void scheduler_exit_current_user_process(int64_t exit_status) {
  ThreadControlBlock* const thread = scheduler_active_thread();
  if (thread != nullptr && thread->execution_mode == kThreadExecutionModeUser &&
      thread->user_mode.kernel_resume_stack_pointer != 0) {
    thread->owner->exit_code = exit_status;
    thread->user_mode.return_value = static_cast<uint64_t>(exit_status);
    user_mode_resume_kernel(thread->user_mode.kernel_resume_stack_pointer,
                            thread->user_mode.kernel_root_physical,
                            static_cast<uint64_t>(exit_status));
  }
  for (;;) {
    disable_interrupts();
    wait_for_interrupt();
  }
}

bool scheduler_handle_user_exception(const InterruptFrame* frame,
                                      uint64_t fault_address) {
  ThreadControlBlock* const thread = scheduler_active_thread();
  if (frame == nullptr || (frame->cs & 3) != 3 || thread == nullptr ||
      thread->execution_mode != kThreadExecutionModeUser || thread->owner == nullptr) {
    return false;
  }
  thread->owner->fault_vector = frame->vector;
  thread->owner->fault_address = fault_address;
  scheduler_exit_current_user_process(128 + static_cast<int64_t>(frame->vector));
}

bool initialize_scheduler(SchedulerState* scheduler,
                          uint32_t time_slice_ticks) {
  if (scheduler == nullptr || time_slice_ticks == 0 || !cpu_initialize()) {
    return false;
  }

  memory_set(scheduler, 0, sizeof(*scheduler));
  cpu_initialize_floating_state(&scheduler->bootstrap_floating_state);
  scheduler->ready = true;
  scheduler->next_pid = 1;
  scheduler->next_tid = 1;
  scheduler->time_slice_ticks = time_slice_ticks;
  scheduler->remaining_slice_ticks = time_slice_ticks;

  for (size_t priority = 0; priority < kSchedulerPriorityCount; ++priority) {
    for (size_t slot = 0; slot < kSchedulerMaxThreadCount; ++slot) {
      scheduler->ready_queue_thread_slots[priority][slot] = kInvalidReadySlot;
    }
  }

  // 调度器初始化时，最关键的一件事不是“马上有普通线程”，
  // 而是先确保系统永远至少有一条可退回的 idle thread。
  ProcessControlBlock* const idle_process = &scheduler->processes[0];
  memory_set(idle_process, 0, sizeof(*idle_process));
  idle_process->in_use = true;
  idle_process->is_kernel_process = true;
  idle_process->pid = 0;
  idle_process->state = kProcessStateReady;
  idle_process->live_thread_count = 1;
  if (!initialize_kernel_address_space_view(&idle_process->address_space)) {
    return false;
  }
  copy_name(idle_process->name, "idle-proc");

  ThreadControlBlock* const idle_thread = &scheduler->threads[0];
  memory_set(idle_thread, 0, sizeof(*idle_thread));
  cpu_initialize_floating_state(&idle_thread->floating_state);
  idle_thread->in_use = true;
  idle_thread->tid = 0;
  idle_thread->state = kThreadStateReady;
  idle_thread->priority = kThreadPriorityIdle;
  idle_thread->owner = idle_process;
  idle_thread->execution_mode = kThreadExecutionModeKernel;
  idle_thread->entry = idle_thread_entry;
  idle_thread->stack_allocation = kmalloc_aligned(
      kSchedulerDefaultKernelThreadStackBytes, 16);
  if (idle_thread->stack_allocation == nullptr) {
    return false;
  }
  idle_thread->stack_allocation_bytes = kSchedulerDefaultKernelThreadStackBytes;
  idle_thread->is_idle_thread = true;
  copy_name(idle_thread->name, "idle");
  prepare_initial_thread_stack(idle_thread);
  initialize_thread_saved_root(scheduler, idle_thread);
  scheduler->idle_thread = idle_thread;

  g_active_scheduler = scheduler;
  return true;
}

bool scheduler_is_ready(const SchedulerState* scheduler) {
  return scheduler != nullptr && scheduler->ready;
}

bool scheduler_set_active(SchedulerState* scheduler) {
  if (scheduler != nullptr && !scheduler_is_ready(scheduler)) {
    return false;
  }

  // 当前教学版本全局只允许 1 个活跃调度器，
  // 所以这里本质上是在切换全局单例指针。
  g_active_scheduler = scheduler;
  return true;
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

  // kernel process 不需要 clone 新页表，
  // 先直接“观察当前内核根页表”就够了。
  if (!initialize_kernel_address_space_view(&process->address_space)) {
    memory_set(process, 0, sizeof(*process));
    return nullptr;
  }
  copy_name(process->name, name);
  return process;
}

ProcessControlBlock* scheduler_create_user_process(
    SchedulerState* scheduler,
    PageAllocator* allocator,
    const char* name) {
  if (!scheduler_is_ready(scheduler) || allocator == nullptr) {
    return nullptr;
  }

  // All user roots must see stable supervisor mappings for kernel heap stacks
  // and filesystem buffers, including pages allocated by later syscalls.
  if (!heap_reserve(kernel_memory_heap(), kKernelHeapLimit - kKernelHeapStart)) {
    return nullptr;
  }

  ProcessControlBlock* const process = first_free_process_slot(scheduler);
  if (process == nullptr) {
    return nullptr;
  }

  memory_set(process, 0, sizeof(*process));
  process->in_use = true;
  process->is_kernel_process = false;
  process->pid = scheduler->next_pid++;
  process->state = kProcessStateReady;

  // user process 和 kernel process 最大区别就在这里：
  // 它要 clone 一份独立页表根，以后才能真的拥有“自己的用户地址空间”。
  process->page_allocator = allocator;
  const ThreadControlBlock* const parent = scheduler->current_thread;
  process->parent_pid = parent != nullptr && parent->owner != nullptr
                            ? parent->owner->pid : 0;
  if (!clone_address_space_from_root(&process->address_space, allocator,
                                      scheduler_kernel_root_physical(scheduler))) {
    memory_set(process, 0, sizeof(*process));
    return nullptr;
  }
  copy_name(process->name, name);
  return process;
}

bool scheduler_initialize_process_syscall_view(
    ProcessControlBlock* process,
    const VfsMount* vfs,
    SyscallWriteHandler write_handler,
    void* write_context) {
  if (process == nullptr || !process->in_use || !vfs_is_mounted(vfs)) {
    return false;
  }

  // 这里把“进程自己的文件视图”和“进程自己的 syscall 视图”第一次绑在一起。
  if (!initialize_file_descriptor_table(&process->file_descriptors, vfs) ||
      !initialize_syscall_context(&process->syscall_context,
                                  &process->file_descriptors)) {
    return false;
  }

  if (write_handler != nullptr &&
      !install_syscall_write_handler(&process->syscall_context,
                                     write_handler,
                                     write_context)) {
    return false;
  }

  return true;
}

bool scheduler_create_user_elf_thread(
    SchedulerState* scheduler,
    PageAllocator* allocator,
    const Os64Fs* filesystem,
    const VfsMount* vfs,
    SyscallWriteHandler write_handler,
    void* write_context,
    const char* process_name,
    const char* thread_name,
    const char* elf_path,
    uint64_t user_stack_pointer,
    uint64_t user_rflags,
    ThreadPriority priority,
    SchedulerElfThreadLoadResult* out_result) {
  if (!scheduler_is_ready(scheduler) ||
      allocator == nullptr ||
      filesystem == nullptr ||
      !vfs_is_mounted(vfs) ||
      elf_path == nullptr ||
      elf_path[0] == '\0' ||
      user_stack_pointer == 0 ||
      out_result == nullptr ||
      !is_runnable_priority(priority)) {
    return false;
  }

  memory_set(out_result, 0, sizeof(*out_result));

  // 这一轮第一次把“正式 ELF 文件”直接推进到 scheduler 持有的 process/thread 上。
  // 调用方不需要再自己手工做：
  // 1. create user process
  // 2. 初始化 syscall/fd/cwd 视图
  // 3. 把 ELF 段装进这份进程地址空间
  // 4. 再准备带保护页的用户栈与动态堆边界
  // 5. 最后创建 user thread
  ProcessControlBlock* const process =
      scheduler_create_user_process(scheduler, allocator, process_name);
  if (process == nullptr ||
      !scheduler_initialize_process_syscall_view(process, vfs,
                                                 write_handler,
                                                 write_context)) {
    if (process != nullptr) {
      (void)scheduler_discard_process(scheduler, process->pid);
    }
    return false;
  }

  LoadedUserElfProgram program;
  memory_set(&program, 0, sizeof(program));
  // 先把 ELF 文件内容和段布局真正装进这份 user process 的地址空间。
  if (!load_elf_user_program(allocator, &process->address_space,
                             filesystem, elf_path, &program)) {
    (void)scheduler_discard_process(scheduler, process->pid);
    return false;
  }

  // 16 张独立物理页组成 64 KiB 用户栈。紧挨栈底的那一页不映射：
  // 当栈向下溢出时，CPU 会触发页错误，终止该用户进程而非破坏堆。
  if (user_stack_pointer != kUserAddressSpaceDefaultStackTop ||
      address_space_resolve_mapping(&process->address_space, kUserStackGuardBase) != 0) {
    (void)scheduler_discard_process(scheduler, process->pid);
    return false;
  }
  uint64_t stack_physical_page = 0;
  for (uint64_t address = kUserStackBottom; address < user_stack_pointer;
       address += kPageSize) {
    const uint64_t physical = alloc_page(allocator);
    void* const bytes = paging_physical_pointer(physical);
    if (physical == 0 || bytes == nullptr) {
      if (physical != 0) (void)free_page(allocator, physical);
      (void)scheduler_discard_process(scheduler, process->pid);
      return false;
    }
    memory_set(bytes, 0, kPageSize);
    if (!address_space_map_user_page(&process->address_space, allocator,
                                    address, physical, kPageWritable | kPageNoExecute)) {
      (void)free_page(allocator, physical);
      (void)scheduler_discard_process(scheduler, process->pid);
      return false;
    }
    if (address == user_stack_pointer - kPageSize) stack_physical_page = physical;
  }
  process->user_heap_base = program.image_end;
  process->user_heap_break = program.image_end;
  process->user_heap_limit = kUserStackGuardBase;

  ThreadControlBlock* const thread =
      scheduler_create_user_thread(scheduler, process, allocator,
                                   thread_name,
                                   program.entry_point,
                                   user_stack_pointer,
                                   user_rflags,
                                   priority);
  if (thread == nullptr) {
    (void)scheduler_discard_process(scheduler, process->pid);
    return false;
  }

  out_result->process = process;
  out_result->thread = thread;
  memory_copy(&out_result->program, &program, sizeof(program));
  out_result->stack_physical_page = stack_physical_page;
  return true;
}

ThreadControlBlock* scheduler_create_kernel_thread(
    SchedulerState* scheduler,
    ProcessControlBlock* owner,
    const char* name,
    KernelThreadEntry entry,
    void* entry_context,
    size_t stack_bytes,
    ThreadPriority priority) {
  if (!scheduler_is_ready(scheduler) || owner == nullptr || !owner->in_use ||
      entry == nullptr || !is_runnable_priority(priority)) {
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

  // 现在调度器已经知道“恢复下一条线程前要先切到哪份 CR3”，
  // 所以 kernel thread 即使挂在 user process 下面，
  // 也不需要再委屈自己去用 identity-mapped 栈。
  //
  // 统一回到普通 kernel heap 栈，更接近真实内核里的线程实现。
  void* const stack_allocation = kmalloc_aligned(stack_bytes, 16);
  if (stack_allocation == nullptr) {
    return nullptr;
  }

  memory_set(thread, 0, sizeof(*thread));
  thread->in_use = true;
  cpu_initialize_floating_state(&thread->floating_state);
  thread->tid = scheduler->next_tid++;
  thread->state = kThreadStateReady;
  thread->priority = priority;
  thread->owner = owner;
  thread->execution_mode = kThreadExecutionModeKernel;
  thread->entry = entry;
  thread->entry_context = entry_context;
  thread->stack_allocation = stack_allocation;
  thread->stack_allocation_bytes = stack_bytes;
  thread->is_idle_thread = false;
  copy_name(thread->name, name);
  prepare_initial_thread_stack(thread);
  initialize_thread_saved_root(scheduler, thread);

  // 到这里线程对象只是“准备好了”；
  // 真正想被调度到，还要进 ready queue。
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

ThreadControlBlock* scheduler_create_user_thread(
    SchedulerState* scheduler,
    ProcessControlBlock* owner,
    PageAllocator* allocator,
    const char* name,
    uint64_t user_instruction_pointer,
    uint64_t user_stack_pointer,
    uint64_t user_rflags,
    ThreadPriority priority) {
  if (!scheduler_is_ready(scheduler) || owner == nullptr || !owner->in_use ||
      allocator == nullptr ||
      owner->is_kernel_process || !owner->address_space.ready ||
      !owner->address_space.owns_page_table_root ||
      user_instruction_pointer == 0 || user_stack_pointer == 0 ||
      !is_runnable_priority(priority)) {
    return nullptr;
  }

  ThreadControlBlock* const thread = first_free_thread_slot(scheduler);
  if (thread == nullptr) {
    return nullptr;
  }

  // Separate supervisor stacks preserve the user_mode_enter return context.
  // The heap is pre-mapped before cloning, so both CR3 roots see these pages.
  void* const scheduler_stack = kmalloc_aligned(kUserKernelStackBytes, 16);
  void* const kernel_entry_stack = kmalloc_aligned(kUserKernelStackBytes, 16);
  if (scheduler_stack == nullptr || kernel_entry_stack == nullptr) {
    if (scheduler_stack != nullptr) {
      (void)kfree(scheduler_stack);
    }
    if (kernel_entry_stack != nullptr) {
      (void)kfree(kernel_entry_stack);
    }
    return nullptr;
  }

  memory_set(thread, 0, sizeof(*thread));
  thread->in_use = true;
  cpu_initialize_floating_state(&thread->floating_state);
  thread->tid = scheduler->next_tid++;
  thread->state = kThreadStateReady;
  thread->priority = priority;
  thread->owner = owner;
  thread->execution_mode = kThreadExecutionModeUser;
  thread->entry = nullptr;
  thread->entry_context = nullptr;
  thread->stack_allocation = scheduler_stack;
  thread->stack_allocation_bytes = kUserKernelStackBytes;
  thread->is_idle_thread = false;
  thread->user_kernel_entry_stack_allocation = kernel_entry_stack;
  thread->user_kernel_entry_stack_top =
      reinterpret_cast<uint64_t>(kernel_entry_stack) + kUserKernelStackBytes;
  thread->user_mode.user_instruction_pointer = user_instruction_pointer;
  thread->user_mode.user_stack_pointer = user_stack_pointer;
  thread->user_mode.user_rflags = (user_rflags & (1ULL << 9)) | 2;
  thread->user_mode.user_code_selector = kUserCodeSelectorRpl3;
  thread->user_mode.user_stack_selector = kUserDataSelectorRpl3;
  copy_name(thread->name, name);
  prepare_initial_thread_stack(thread);
  initialize_thread_saved_root(scheduler, thread);

  // user thread 也要先进 ready queue，
  // 之后第一次被切进去时，才会真正通过 bootstrap -> user_mode_enter() 落到 ring 3。
  ++owner->live_thread_count;
  ++scheduler->live_thread_count;

  if (!push_ready_thread(scheduler, thread)) {
    thread->in_use = false;
    --owner->live_thread_count;
    --scheduler->live_thread_count;
    (void)kfree(scheduler_stack);
    (void)kfree(kernel_entry_stack);
    return nullptr;
  }

  return thread;
}

bool scheduler_run_until_idle(SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread != nullptr) {
    return false;
  }

  // 这条路径主要给 smoke test 用：
  // 从 bootstrap 栈切进第一条 ready 线程，然后一路跑到系统里再也没有活线程/睡眠线程/阻塞线程。
  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread == nullptr) {
    return false;
  }

  switch_from_bootstrap_to_thread(scheduler, next_thread);
  reap_orphans(scheduler);
  return scheduler->live_thread_count == 0 &&
         scheduler->sleeping_thread_count == 0 &&
         scheduler->blocked_thread_count == 0;
}

bool scheduler_yield_current_thread() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr) {
    return false;
  }

  // `yield` 的核心语义是：
  // 当前线程自己把 CPU 还回去，如果 ready queue 里还有别人，就让别人先跑。
  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread->state != kThreadStateRunning) {
    return false;
  }

  if (current_thread->is_idle_thread) {
    // idle thread 的 yield 语义是“看看有没有普通线程已经准备好了”。
    ThreadControlBlock* const next_thread = pop_highest_ready_thread(scheduler);
    if (next_thread == nullptr) {
      scheduler->preempt_requested = false;
      scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
      return false;
    }

    switch_thread_context(scheduler, current_thread, next_thread);
    return true;
  }

  if (scheduler->ready_count == 0) {
    scheduler->preempt_requested = false;
    scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
    return false;
  }

  current_thread->state = kThreadStateReady;
  ++current_thread->yield_count;
  ++scheduler->total_yields;
  update_owner_ready_state(current_thread->owner);

  // 把自己重新塞回 ready queue 末尾，才能形成同优先级 round-robin。
  if (!push_ready_thread(scheduler, current_thread)) {
    current_thread->state = kThreadStateRunning;
    return false;
  }

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread == nullptr) {
    current_thread->state = kThreadStateRunning;
    scheduler->current_thread = current_thread;
    return false;
  }

  switch_thread_context(scheduler, current_thread, next_thread);
  return true;
}

bool scheduler_yield_if_requested() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || !scheduler->preempt_requested ||
      scheduler->current_thread == nullptr) {
    return false;
  }

  // timer IRQ 不直接在中断里切线程，而是先把“该切换了”记成一个请求位。
  // 这里就是在线程上下文里真正兑现这个请求。
  return scheduler_yield_current_thread();
}

bool scheduler_sleep_current_thread(uint64_t ticks) {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr ||
      scheduler->current_thread->is_idle_thread) {
    return false;
  }

  // `sleep` 和 `yield` 的区别是：
  // yield 立刻还能被选回 ready，
  // sleep 则要等 timer tick 到了 wake_tick 才能被唤醒。
  if (ticks == 0) {
    return scheduler_yield_current_thread();
  }

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  current_thread->state = kThreadStateSleeping;
  current_thread->wake_tick = scheduler->total_ticks + ticks;
  if (current_thread->wake_tick <= scheduler->total_ticks) {
    current_thread->wake_tick = scheduler->total_ticks + 1;
  }
  ++scheduler->sleeping_thread_count;
  update_owner_ready_state(current_thread->owner);

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread == nullptr) {
    current_thread->state = kThreadStateRunning;
    current_thread->wake_tick = 0;
    if (scheduler->sleeping_thread_count > 0) {
      --scheduler->sleeping_thread_count;
    }
    return false;
  }

  switch_thread_context(scheduler, current_thread, next_thread);
  return true;
}

bool block_current_thread_internal(bool enable_interrupts_before_switch) {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr ||
      scheduler->current_thread->is_idle_thread) {
    return false;
  }

  // `blocked` 和 `sleeping` 的区别是：
  // sleeping 等时间，
  // blocked 等事件，比如键盘输入到来。
  ThreadControlBlock* const current_thread = scheduler->current_thread;
  current_thread->state = kThreadStateBlocked;
  current_thread->wake_tick = 0;
  ++scheduler->blocked_thread_count;
  update_owner_ready_state(current_thread->owner);

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread == nullptr) {
    current_thread->state = kThreadStateRunning;
    if (scheduler->blocked_thread_count > 0) {
      --scheduler->blocked_thread_count;
    }

    if (enable_interrupts_before_switch) {
      enable_interrupts();
    }
    return false;
  }

  mark_thread_running(scheduler, next_thread);
  ++scheduler->total_switches;

  if (enable_interrupts_before_switch) {
    // 这个分支专门服务“先登记等待，再让 IRQ 真能把我唤醒”的场景。
    enable_interrupts();
  }

  switch_thread_context(scheduler, current_thread, next_thread);
  return true;
}

bool scheduler_block_current_thread() {
  return block_current_thread_internal(false);
}

bool scheduler_block_current_thread_and_enable_interrupts() {
  return block_current_thread_internal(true);
}

bool scheduler_wake_thread(ThreadControlBlock* thread) {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    return false;
  }

  // 对外公开的 wake 接口，本质上只是包了一层 active scheduler 查找。
  return wake_thread_internal(scheduler, thread, true);
}

[[noreturn]] void scheduler_exit_current_thread() {
  disable_interrupts();
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr) {
    for (;;) {
      wait_for_interrupt();
    }
  }

  // 线程结束时，不是直接把整个进程立刻抹掉，
  // 而是先减 live_thread_count；只有最后一条线程退出时，进程才标成 exited。
  ThreadControlBlock* const current_thread = scheduler->current_thread;
  current_thread->state = kThreadStateFinished;
  current_thread->wake_tick = 0;

  if (!current_thread->is_idle_thread) {
    if (current_thread->owner != nullptr) {
      if (current_thread->owner->live_thread_count > 0) {
        --current_thread->owner->live_thread_count;
      }

      if (current_thread->owner->live_thread_count == 0) {
        kernel_log_process_event(kLogDebug, "exit", current_thread->owner->pid);
        // 退出立即释放管道引用。若拖到父进程 wait/reap，读者可能永远等不到 EOF。
        fd_close_all(&current_thread->owner->file_descriptors);
        if (!current_thread->owner->is_kernel_process)
          network_udp_close_owner(current_thread->owner->pid);
        current_thread->owner->state = kProcessStateExited;
        const uint32_t pid = current_thread->owner->pid;
        for (auto& child : scheduler->processes) {
          if (child.in_use && child.parent_pid == pid) {
            child.parent_pid = 0;
            child.auto_reap = true;
          }
        }
        for (auto& waiter : scheduler->threads) {
          if (waiter.in_use && waiter.state == kThreadStateBlocked &&
              waiter.waiting_for_pid == pid) {
            (void)wake_thread_internal(scheduler, &waiter, false);
          }
        }
      } else {
        current_thread->owner->state = kProcessStateReady;
      }
    }

    if (scheduler->live_thread_count > 0) {
      --scheduler->live_thread_count;
    }
  }

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread != nullptr) {
    // 如果还有别人能跑，就直接切过去；
    // 当前这条 finished 线程不会再回来。
    switch_thread_context(scheduler, current_thread, next_thread);
  }

  scheduler->current_thread = nullptr;
  scheduler->preempt_requested = false;
  scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  switch_from_thread_to_bootstrap(scheduler, current_thread);

  for (;;) {
    wait_for_interrupt();
  }
}

void scheduler_handle_timer_tick() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    return;
  }

  // timer tick 这层先只做两类事：
  // 1. 记账和唤醒 sleep 线程
  // 2. 时间片耗尽时，发出一个“应该尽快切换”的请求
  ++scheduler->total_ticks;
  wake_sleeping_threads(scheduler);

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread == nullptr ||
      current_thread->state != kThreadStateRunning) {
    return;
  }

  if (current_thread == scheduler->idle_thread) {
    if (scheduler->ready_count > 0 && !scheduler->preempt_requested) {
      scheduler->preempt_requested = true;
      ++scheduler->preempt_request_count;
    }
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

  // 真正切换不在 IRQ 里直接做，而是只打一个请求位，
  // 后面在更安全的线程上下文里通过 `scheduler_yield_if_requested()` 完成。
  scheduler->remaining_slice_ticks = scheduler->time_slice_ticks;
  if (scheduler->ready_count == 0) {
    return;
  }

  if (!scheduler->preempt_requested) {
    scheduler->preempt_requested = true;
    ++scheduler->preempt_request_count;
  }
}

ThreadControlBlock* scheduler_current_thread(
    const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return nullptr;
  }

  return scheduler->current_thread;
}

ThreadControlBlock* scheduler_active_thread() {
  return scheduler_current_thread(active_scheduler());
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

uint32_t scheduler_sleeping_thread_count(const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return 0;
  }

  return scheduler->sleeping_thread_count;
}

uint32_t scheduler_blocked_thread_count(const SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler)) {
    return 0;
  }

  return scheduler->blocked_thread_count;
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
    case kThreadStateSleeping:
      return "sleeping";
    case kThreadStateBlocked:
      return "blocked";
    case kThreadStateFinished:
      return "finished";
    default:
      return "invalid";
  }
}

const char* scheduler_thread_priority_name(ThreadPriority priority) {
  switch (priority) {
    case kThreadPriorityHigh:
      return "high";
    case kThreadPriorityNormal:
      return "normal";
    case kThreadPriorityBackground:
      return "background";
    case kThreadPriorityIdle:
      return "idle";
    default:
      return "invalid";
  }
}

void run_current_user_thread(ThreadControlBlock* current_thread) {
  if (!is_user_thread_ready_to_enter(current_thread)) {
    return;
  }

  // 到这里，调度器已经把“下一条该跑的线程”选好了；
  // `user_mode_enter()` 才是真正把 CPU 从内核态送进 ring 3 的最后一步。
  current_thread->user_mode.kernel_root_physical =
      scheduler_kernel_root_physical(active_scheduler());
  current_thread->user_mode.user_root_physical =
      current_thread->owner->address_space.root_physical_address;
  current_thread->user_mode.return_value = 0;

  const uint64_t return_value = user_mode_enter(&current_thread->user_mode);
  current_thread->user_mode.return_value = return_value;
  current_thread->owner->exit_code = static_cast<int64_t>(return_value);
}

extern "C" void scheduler_thread_bootstrap() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    for (;;) {
      wait_for_interrupt();
    }
  }
  reap_orphans(scheduler);

  // 每条线程第一次被调度进来时，都会先落到这个统一 bootstrap：
  // - user thread：走 `run_current_user_thread()`
  // - kernel thread：调它自己的 entry(context)
  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread == nullptr) {
    scheduler_exit_current_thread();
  }

  if (current_thread->execution_mode == kThreadExecutionModeUser) {
    // user thread 没有普通的 C++ entry 函数，
    // 它的“入口”就是准备好的 user RIP/RSP 和那份用户地址空间。
    run_current_user_thread(current_thread);
    scheduler_exit_current_thread();
  }

  if (current_thread->entry == nullptr) {
    scheduler_exit_current_thread();
  }

  current_thread->entry(current_thread->entry_context);
  scheduler_exit_current_thread();
}
