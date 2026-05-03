#include "task/scheduler.hpp"

#include "boot/segments.hpp"
#include "interrupts/interrupts.hpp"
#include "memory/kmemory.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

namespace {

constexpr uint8_t kInvalidReadySlot = 0xFF;
constexpr size_t kMinimumThreadStackBytes = 4096;

SchedulerState* g_active_scheduler = nullptr;  // 当前系统里先只保留 1 个全局活跃调度器。

extern "C" void scheduler_switch_context_and_root(
    uint64_t* saved_stack_pointer,
    uint64_t load_stack_pointer,
    uint64_t load_root_physical);
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

bool page_is_directly_accessible_in_boot_identity_map(uint64_t physical_address) {
  return physical_address != 0 && physical_address < kPagingBootIdentityLimit;
}

uint64_t scheduler_kernel_root_physical(const SchedulerState* scheduler) {
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
                                    next_root);
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
                                    next_root);
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
                                    scheduler_kernel_root_physical(scheduler));
}

void update_owner_ready_state(ProcessControlBlock* owner) {
  if (owner == nullptr) {
    return;
  }

  if (owner->state == kProcessStateRunning) {
    owner->state = kProcessStateReady;
  }
}

ThreadControlBlock* select_next_runnable_thread(SchedulerState* scheduler) {
  if (scheduler == nullptr) {
    return nullptr;
  }

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
  for (;;) {
    wait_for_interrupt();
    (void)scheduler_yield_if_requested();
  }
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

  for (size_t priority = 0; priority < kSchedulerPriorityCount; ++priority) {
    for (size_t slot = 0; slot < kSchedulerMaxThreadCount; ++slot) {
      scheduler->ready_queue_thread_slots[priority][slot] = kInvalidReadySlot;
    }
  }

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

  ProcessControlBlock* const process = first_free_process_slot(scheduler);
  if (process == nullptr) {
    return nullptr;
  }

  memory_set(process, 0, sizeof(*process));
  process->in_use = true;
  process->is_kernel_process = false;
  process->pid = scheduler->next_pid++;
  process->state = kProcessStateReady;
  if (!clone_current_address_space(&process->address_space, allocator)) {
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
  // 4. 再单独补一页用户栈
  // 5. 最后创建 user thread
  ProcessControlBlock* const process =
      scheduler_create_user_process(scheduler, allocator, process_name);
  if (process == nullptr ||
      !scheduler_initialize_process_syscall_view(process, vfs,
                                                 write_handler,
                                                 write_context)) {
    return false;
  }

  LoadedUserElfProgram program;
  memory_set(&program, 0, sizeof(program));
  if (!load_elf_user_program(allocator, &process->address_space,
                             filesystem, elf_path, &program)) {
    return false;
  }

  const uint64_t stack_physical_page = alloc_page(allocator);
  if (!page_is_directly_accessible_in_boot_identity_map(stack_physical_page)) {
    return false;
  }

  memory_set(reinterpret_cast<void*>(
                 static_cast<uintptr_t>(stack_physical_page)),
             0,
             kPagingPageSize);

  // 这里先继续保持“1 条线程只有 1 页初始用户栈”的教学布局。
  // 当前调用方传进来的通常是那一页顶端，例如 0x800000。
  // 所以真正要映射的是它下面那页页框起点。
  const uint64_t stack_page_virtual_address =
      align_down(user_stack_pointer - 1, kPagingPageSize);
  if (!address_space_map_user_page(&process->address_space, allocator,
                                   stack_page_virtual_address,
                                   stack_physical_page,
                                   kPageWritable)) {
    return false;
  }

  ThreadControlBlock* const thread =
      scheduler_create_user_thread(scheduler, process, allocator,
                                   thread_name,
                                   program.entry_point,
                                   user_stack_pointer,
                                   user_rflags,
                                   priority);
  if (thread == nullptr) {
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

  // user thread 进入 ring 3 之前，第一版其实需要两根 low identity-mapped 栈：
  // 1. scheduler/bootstrap 栈：
  //    - `scheduler_switch_context()` 会从这里第一次把线程“ret”进 bootstrap
  //    - `user_mode_enter()` 也会把“最后要退回哪条内核栈”的返回现场保存在这里
  // 2. TSS.rsp0 专用内核进入栈：
  //    - 每次用户态 `int 0x80` / 以后外部中断进 ring 0，都先落到这根栈
  //
  // 如果只给 1 根栈，那么后续 syscall 压入的 trap frame 会覆盖掉
  // `user_mode_enter()` 当初保存的最终返回现场，最后 `exit` 就没法安全回到线程入口了。
  //
  // 这里不能直接用 heap 栈，因为当前教学内核的 heap 虚拟区和用户区窗口还有重叠，
  // clone 出来的 user root 不一定能继续看到那根 heap 栈。
  //
  // 所以第一版先保守地给 user thread 分 2 页低地址 identity-mapped 栈：
  // - 在当前 kernel root 下能访问
  // - 切到 cloned user root 之后也仍然有同样的恒等映射
  const uint64_t scheduler_stack_page = alloc_page(allocator);
  const uint64_t kernel_entry_stack_page = alloc_page(allocator);
  if (scheduler_stack_page == 0 ||
      scheduler_stack_page >= kPagingBootIdentityLimit ||
      kernel_entry_stack_page == 0 ||
      kernel_entry_stack_page >= kPagingBootIdentityLimit) {
    return nullptr;
  }

  memory_set(reinterpret_cast<void*>(
                 static_cast<uintptr_t>(scheduler_stack_page)),
             0, kPagingPageSize);
  memory_set(reinterpret_cast<void*>(
                 static_cast<uintptr_t>(kernel_entry_stack_page)),
             0,
             kPagingPageSize);

  memory_set(thread, 0, sizeof(*thread));
  thread->in_use = true;
  thread->tid = scheduler->next_tid++;
  thread->state = kThreadStateReady;
  thread->priority = priority;
  thread->owner = owner;
  thread->execution_mode = kThreadExecutionModeUser;
  thread->entry = nullptr;
  thread->entry_context = nullptr;
  thread->stack_allocation =
      reinterpret_cast<void*>(static_cast<uintptr_t>(scheduler_stack_page));
  thread->stack_allocation_bytes = kPagingPageSize;
  thread->is_idle_thread = false;
  thread->user_kernel_entry_stack_allocation =
      reinterpret_cast<void*>(static_cast<uintptr_t>(kernel_entry_stack_page));
  thread->user_kernel_entry_stack_top = kernel_entry_stack_page + kPagingPageSize;
  thread->user_mode.user_instruction_pointer = user_instruction_pointer;
  thread->user_mode.user_stack_pointer = user_stack_pointer;
  thread->user_mode.user_rflags = user_rflags;
  thread->user_mode.user_code_selector = kUserCodeSelectorRpl3;
  thread->user_mode.user_stack_selector = kUserDataSelectorRpl3;
  copy_name(thread->name, name);
  prepare_initial_thread_stack(thread);
  initialize_thread_saved_root(scheduler, thread);

  ++owner->live_thread_count;
  ++scheduler->live_thread_count;

  if (!push_ready_thread(scheduler, thread)) {
    thread->in_use = false;
    --owner->live_thread_count;
    --scheduler->live_thread_count;
    return nullptr;
  }

  return thread;
}

bool scheduler_run_until_idle(SchedulerState* scheduler) {
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread != nullptr) {
    return false;
  }

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread == nullptr) {
    return false;
  }

  switch_from_bootstrap_to_thread(scheduler, next_thread);
  return scheduler->live_thread_count == 0 &&
         scheduler->sleeping_thread_count == 0 &&
         scheduler->blocked_thread_count == 0;
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

  if (current_thread->is_idle_thread) {
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

  return scheduler_yield_current_thread();
}

bool scheduler_sleep_current_thread(uint64_t ticks) {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler) || scheduler->current_thread == nullptr ||
      scheduler->current_thread->is_idle_thread) {
    return false;
  }

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

  return wake_thread_internal(scheduler, thread, true);
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
  current_thread->wake_tick = 0;

  if (!current_thread->is_idle_thread) {
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
  }

  ThreadControlBlock* const next_thread = select_next_runnable_thread(scheduler);
  if (next_thread != nullptr) {
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

  current_thread->user_mode.kernel_root_physical =
      scheduler_kernel_root_physical(active_scheduler());
  current_thread->user_mode.user_root_physical =
      current_thread->owner->address_space.root_physical_address;
  current_thread->user_mode.return_value = 0;

  const uint64_t return_value = user_mode_enter(&current_thread->user_mode);
  current_thread->user_mode.return_value = return_value;
}

extern "C" void scheduler_thread_bootstrap() {
  SchedulerState* const scheduler = active_scheduler();
  if (!scheduler_is_ready(scheduler)) {
    for (;;) {
      wait_for_interrupt();
    }
  }

  ThreadControlBlock* const current_thread = scheduler->current_thread;
  if (current_thread == nullptr) {
    scheduler_exit_current_thread();
  }

  if (current_thread->execution_mode == kThreadExecutionModeUser) {
    run_current_user_thread(current_thread);
    scheduler_exit_current_thread();
  }

  if (current_thread->entry == nullptr) {
    scheduler_exit_current_thread();
  }

  current_thread->entry(current_thread->entry_context);
  scheduler_exit_current_thread();
}
