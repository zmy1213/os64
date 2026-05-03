#ifndef OS64_SCHEDULER_HPP
#define OS64_SCHEDULER_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 第一版 tasking 先故意保守一点：
// 只支持很少量的进程和线程，避免一上来把重点淹没在“可变长容器”里。
constexpr size_t kSchedulerMaxProcessCount = 8;
constexpr size_t kSchedulerMaxThreadCount = 16;
constexpr size_t kSchedulerNameCapacity = 24;
constexpr size_t kSchedulerDefaultKernelThreadStackBytes = 8192;
constexpr size_t kSchedulerPriorityCount = 4;

// 这一轮的进程状态先只保留最关键的几档：
// - free: 槽位还没被用过
// - ready: 还有活线程，但此刻没有线程正在 CPU 上跑
// - running: 当前至少有一个线程正在执行
// - exited: 所有线程都结束了，但元数据还保留着给观察/调试用
enum ProcessState : uint8_t {
  kProcessStateFree = 0,
  kProcessStateReady = 1,
  kProcessStateRunning = 2,
  kProcessStateExited = 3,
};

// 线程状态会更细一点，因为调度器真正切的是线程。
enum ThreadState : uint8_t {
  kThreadStateFree = 0,
  kThreadStateReady = 1,
  kThreadStateRunning = 2,
  kThreadStateSleeping = 3,
  kThreadStateBlocked = 4,
  kThreadStateFinished = 5,
};

// 第一版更合理的调度形状不是“所有线程全都扔进一个桶里”，
// 而是先给线程一个粗粒度优先级，再在同优先级里做 round-robin。
enum ThreadPriority : uint8_t {
  kThreadPriorityHigh = 0,
  kThreadPriorityNormal = 1,
  kThreadPriorityBackground = 2,
  kThreadPriorityIdle = 3,
};

using KernelThreadEntry = void (*)(void* context);

struct ProcessControlBlock {
  bool in_use;                                     // 这个 PCB 槽位现在是否已经被占用。
  bool is_kernel_process;                          // 这一步还没有用户态，所以先只区分“是不是内核进程”。
  uint32_t pid;                                    // 第一版进程号，后面 shell/ps/等待机制都会靠它识别对象。
  ProcessState state;                              // 当前进程大体处在什么生命周期阶段。
  uint32_t live_thread_count;                      // 这个进程还有多少线程没退出。
  uint64_t total_thread_ticks;                     // 这个进程名下所有线程一共消耗了多少 timer tick。
  uint64_t dispatch_count;                         // 这个进程的线程一共被调度上 CPU 多少次。
  char name[kSchedulerNameCapacity];               // 先保留一个固定长度名字，方便日志和调试。
};

struct ThreadControlBlock {
  bool in_use;                                     // 这个 TCB 槽位现在是否已经被占用。
  uint32_t tid;                                    // 第一版线程号。
  ThreadState state;                               // 当前线程所处的调度状态。
  ThreadPriority priority;                         // 调度器现在会先看优先级，再看队列顺序。
  ProcessControlBlock* owner;                      // 这个线程属于哪个进程。
  KernelThreadEntry entry;                         // 真正的线程入口函数。
  void* entry_context;                             // 交给入口函数的参数。
  void* stack_allocation;                          // 线程栈底层来自哪块堆内存，后面做回收会用到它。
  size_t stack_allocation_bytes;                   // 栈一共分了多少字节。
  uint64_t stack_top;                              // 栈顶虚拟地址，主要给初始化栈帧用。
  uint64_t saved_stack_pointer;                    // 这就是上下文切换时真正来回保存/恢复的 RSP。
  uint64_t dispatch_count;                         // 这个线程被切上 CPU 多少次。
  uint64_t yield_count;                            // 这个线程主动/被请求让出 CPU 多少次。
  uint64_t consumed_ticks;                         // 这个线程在运行态下累计消耗了多少 timer tick。
  uint64_t wake_tick;                              // 如果线程在 sleep，这里记录“第几个 tick 应该被唤醒”。
  bool is_idle_thread;                             // idle thread 是特殊线程：永远存在，但不计入普通 live thread。
  char name[kSchedulerNameCapacity];               // 线程名字先也做成固定数组，避免早期依赖动态字符串。
};

struct SchedulerState {
  bool ready;                                      // 调度器是否已经初始化完成。
  uint32_t next_pid;                               // 下一个可分配的 PID。
  uint32_t next_tid;                               // 下一个可分配的 TID。
  uint32_t time_slice_ticks;                       // 一个时间片先按多少个 tick 算。
  uint32_t remaining_slice_ticks;                  // 当前线程这一片还剩多少 tick。
  bool preempt_requested;                          // IRQ 路径只先发“该换人了”的请求，不直接在中断里切栈。
  uint32_t ready_head[kSchedulerPriorityCount];    // 每个优先级都有自己的 ready queue。
  uint32_t ready_tail[kSchedulerPriorityCount];
  uint32_t ready_count_by_priority[kSchedulerPriorityCount];
  uint32_t ready_count;                            // 所有优先级加起来，一共多少 ready 线程。
  uint32_t live_thread_count;                      // 全系统现在还有多少线程活着。
  uint32_t sleeping_thread_count;                  // 当前正在 sleep 的线程数。
  uint32_t blocked_thread_count;                   // 当前被事件阻塞的线程数。
  uint64_t total_ticks;                            // 调度器看到的总 timer tick 数。
  uint64_t total_switches;                         // 总共发生了多少次线程切换。
  uint64_t total_yields;                           // 总共发生了多少次 yield。
  uint64_t preempt_request_count;                  // timer 一共发出过多少次“建议换人”的请求。
  uint64_t bootstrap_stack_pointer;                // 从 kernel_main 进入 scheduler 前的那条原始栈。
  ThreadControlBlock* current_thread;              // 当前真正在 CPU 上跑的线程。
  ThreadControlBlock* idle_thread;                 // 没有普通线程可运行时，就落到 idle thread。
  uint8_t ready_queue_thread_slots[kSchedulerPriorityCount]
                                  [kSchedulerMaxThreadCount];
  ProcessControlBlock processes[kSchedulerMaxProcessCount];
  ThreadControlBlock threads[kSchedulerMaxThreadCount];
};

bool initialize_scheduler(SchedulerState* scheduler,
                          uint32_t time_slice_ticks);
bool scheduler_is_ready(const SchedulerState* scheduler);

ProcessControlBlock* scheduler_create_kernel_process(
    SchedulerState* scheduler,
    const char* name);

ThreadControlBlock* scheduler_create_kernel_thread(
    SchedulerState* scheduler,
    ProcessControlBlock* owner,
    const char* name,
    KernelThreadEntry entry,
    void* entry_context,
    size_t stack_bytes,
    ThreadPriority priority);

// 让当前 ready queue 一直跑到“已经没有任何可运行线程”为止。
// 第一版 smoke test 会先靠它证明：
// 1. 不同线程真的有各自的独立栈
// 2. 调度器真的会在线程之间切换
bool scheduler_run_until_idle(SchedulerState* scheduler);

// 这两个接口只应该在线程上下文里调用：
// - yield：当前线程主动让出 CPU
// - yield_if_requested：如果 timer 已经发出“该换人”请求，就在安全点切换
bool scheduler_yield_current_thread();
bool scheduler_yield_if_requested();
bool scheduler_sleep_current_thread(uint64_t ticks);
bool scheduler_block_current_thread();
// 这个入口专门给“先关中断登记等待者，再真正 block 自己”的等待队列用。
// 它会在切到下一个线程前重新开中断，避免系统因为当前线程睡下去而收不到唤醒它的外部 IRQ。
bool scheduler_block_current_thread_and_enable_interrupts();
bool scheduler_wake_thread(ThreadControlBlock* thread);

// 由 timer IRQ 路径调用。
// 这一轮它先只负责：
// 1. 给当前线程记账
// 2. 在时间片用完时发出“该尽快切换”的请求
void scheduler_handle_timer_tick();

ThreadControlBlock* scheduler_current_thread(
    const SchedulerState* scheduler);
ThreadControlBlock* scheduler_active_thread();
uint32_t scheduler_ready_thread_count(const SchedulerState* scheduler);
uint32_t scheduler_live_thread_count(const SchedulerState* scheduler);
uint32_t scheduler_sleeping_thread_count(const SchedulerState* scheduler);
uint32_t scheduler_blocked_thread_count(const SchedulerState* scheduler);

const char* scheduler_process_state_name(ProcessState state);
const char* scheduler_thread_state_name(ThreadState state);
const char* scheduler_thread_priority_name(ThreadPriority priority);

#endif
