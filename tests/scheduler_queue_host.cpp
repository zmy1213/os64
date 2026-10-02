// Exercise the production private helpers directly. Dead stripping excludes
// unrelated privileged/context-switch code; only IRQ/CPU observation is mocked.
#include <cassert>
#include <cstdio>
#include "../kernel/task/scheduler.cpp"

static uint32_t simulated_cpu=0;
static uint32_t simulated_online=4;
static bool simulated_smp=false, irq_enabled=true;
static uint64_t simulated_pit_ticks=100;
uint64_t timer_tick_count() { return simulated_pit_ticks; }
static uint32_t notifications[64];
static size_t notification_count=0;
uint32_t smp_current_cpu_index() { return simulated_cpu; }
uint32_t smp_online_cpu_count() { return simulated_online; }
bool smp_is_enabled() { return simulated_smp; }
void smp_send_reschedule(uint32_t target) { assert(notification_count<64);notifications[notification_count++]=target; }
void disable_interrupts() { irq_enabled=false; }
void enable_interrupts() { irq_enabled=true; }
bool interrupts_are_enabled() { return irq_enabled; }

static void reset(SchedulerState& state) {
  state={};state.ready=true;state.smp_cpu_count=4;state.time_slice_ticks=2;
  simulated_cpu=0;simulated_online=4;simulated_smp=false;irq_enabled=true;simulated_pit_ticks=100;notification_count=0;
}
static ThreadControlBlock* thread(SchedulerState& state,size_t slot,uint32_t cpu,
                                  ThreadPriority priority=kThreadPriorityNormal,
                                  ThreadExecutionMode mode=kThreadExecutionModeUser) {
  auto* t=&state.threads[slot];*t={};t->in_use=true;t->tid=slot;
  t->state=kThreadStateReady;t->priority=priority;t->execution_mode=mode;t->assigned_cpu=cpu;
  return t;
}
static void fifo_and_no_notification() {
  SchedulerState state;reset(state);simulated_smp=true;simulated_cpu=2;
  auto* a=thread(state,1,1);auto* b=thread(state,2,0);auto* c=thread(state,3,1);
  assert(push_ready_thread(&state,a));assert(push_ready_thread(&state,b));assert(push_ready_thread(&state,c));
  assert(!push_ready_thread(&state,a) && state.ready_count==3);
  assert(notification_count==3);notification_count=0;
  simulated_cpu=3;irq_enabled=false;
  assert(pop_highest_ready_thread(&state)==nullptr && state.ready_count==3 && !irq_enabled);
  simulated_cpu=0;irq_enabled=true;
  assert(pop_highest_ready_thread(&state)==b && irq_enabled && !b->queued);
  simulated_cpu=1;
  assert(pop_highest_ready_thread(&state)==a);assert(pop_highest_ready_thread(&state)==c);
  assert(pop_highest_ready_thread(&state)==nullptr && state.ready_count==0);
  assert(notification_count==0); // scanning cannot emit a remote IPI storm.
}
static void priorities_and_kernel_affinity() {
  SchedulerState state;reset(state);
  auto* kernel=thread(state,1,0,kThreadPriorityHigh,kThreadExecutionModeKernel);
  auto* normal=thread(state,2,2);auto* background=thread(state,3,2,kThreadPriorityBackground);
  assert(push_ready_thread(&state,background));assert(push_ready_thread(&state,kernel));assert(push_ready_thread(&state,normal));
  simulated_cpu=2;assert(pop_highest_ready_thread(&state)==normal);
  assert(pop_highest_ready_thread(&state)==background);assert(pop_highest_ready_thread(&state)==nullptr);
  simulated_cpu=0;assert(pop_highest_ready_thread(&state)==kernel);assert(state.ready_count==0);
}
static void balance_and_pin() {
  SchedulerState state;reset(state);
  for(size_t i=1;i<=3;++i) thread(state,i,i==3?1:0)->state=kThreadStateRunning;
  auto* a=thread(state,4,kSchedulerUnassignedCpu);auto* b=thread(state,5,kSchedulerUnassignedCpu);
  assert(push_ready_thread(&state,a));assert(push_ready_thread(&state,b));
  simulated_cpu=0;assert(pop_highest_ready_thread(&state)==nullptr);
  simulated_cpu=2;assert(pop_highest_ready_thread(&state)==a && a->assigned_cpu==2);
  simulated_cpu=3;assert(pop_highest_ready_thread(&state)==b && b->assigned_cpu==3);
  a->state=kThreadStateReady;assert(push_ready_thread(&state,a));
  simulated_cpu=0;assert(pop_highest_ready_thread(&state)==nullptr);
  simulated_cpu=2;assert(pop_highest_ready_thread(&state)==a && a->assigned_cpu==2);
}
static void circular_fifo_stress() {
  SchedulerState state;reset(state);ThreadControlBlock* order[31];size_t order_count=0;
  for(size_t i=0;i<31;++i) {
    const size_t slot=1+(i*17)%31;auto* t=thread(state,slot,slot%4);
    assert(push_ready_thread(&state,t));order[order_count++]=t;
  }
  assert(!push_ready_thread(&state,thread(state,0,0))); // capacity, not idle rejection.
  for(size_t iteration=0;iteration<2000;++iteration) {
    simulated_cpu=(iteration*7+iteration/9)%4;
    size_t match=0;while(match<order_count && order[match]->assigned_cpu!=simulated_cpu) ++match;
    assert(match<order_count);auto* expected=order[match];
    for(size_t i=match+1;i<order_count;++i) order[i-1]=order[i];--order_count;
    assert(pop_highest_ready_thread(&state)==expected && !expected->queued && state.ready_count==30);
    expected->state=kThreadStateReady;assert(push_ready_thread(&state,expected));order[order_count++]=expected;
    assert(!push_ready_thread(&state,expected) && state.ready_count==31);
    for(size_t i=0;i<order_count;++i) assert(order[i]->queued);
  }
  for(size_t cpu=0;cpu<4;++cpu) {
    simulated_cpu=cpu;
    for(;;) {
      size_t match=0;while(match<order_count && order[match]->assigned_cpu!=cpu) ++match;
      if(match==order_count) { assert(pop_highest_ready_thread(&state)==nullptr);break; }
      auto* expected=order[match];
      for(size_t i=match+1;i<order_count;++i) order[i-1]=order[i];--order_count;
      assert(pop_highest_ready_thread(&state)==expected);
    }
  }
  assert(order_count==0 && state.ready_count==0);
}
static void deadlines_and_local_ticks() {
  SchedulerState state;reset(state);g_active_scheduler=&state;state.total_ticks=19;
  uint64_t normalized=UINT64_MAX;
  assert(relative_block_deadline(&state,105,&normalized) && normalized==24);
  assert(!relative_block_deadline(&state,100,&normalized));
  assert(!relative_block_deadline(&state,99,&normalized));
  assert(relative_block_deadline(&state,0,&normalized) && normalized==0);
  assert(relative_block_deadline(&state,UINT64_MAX,&normalized) && normalized==0);
  state.total_ticks=UINT64_MAX-3;
  assert(!relative_block_deadline(&state,104,&normalized));state.total_ticks=19;
  assert(relative_block_deadline(&state,101,&normalized) && normalized==20);
  auto* finite=thread(state,1,1);finite->state=kThreadStateBlocked;finite->wake_tick=normalized;
  auto* asleep=thread(state,2,2);asleep->state=kThreadStateSleeping;asleep->wake_tick=21;
  auto* infinite=thread(state,3,3);infinite->state=kThreadStateBlocked;infinite->wake_tick=0;
  state.blocked_thread_count=2;state.sleeping_thread_count=1;
  wake_sleeping_threads(&state);assert(state.ready_count==0);
  state.total_ticks=20;wake_sleeping_threads(&state);
  assert(finite->state==kThreadStateReady && finite->wake_tick==0 && finite->queued);
  assert(state.blocked_thread_count==1 && state.sleeping_thread_count==1);
  state.total_ticks=21;wake_sleeping_threads(&state);
  assert(asleep->state==kThreadStateReady && asleep->wake_tick==0 && state.sleeping_thread_count==0);
  assert(infinite->state==kThreadStateBlocked && state.blocked_thread_count==1);
  simulated_cpu=1;auto* current=thread(state,4,1);current->state=kThreadStateRunning;
  state.secondary_cpus[0].current_thread=current;state.secondary_cpus[0].remaining_slice_ticks=2;
  scheduler_handle_local_timer_tick();scheduler_handle_local_timer_tick();
  assert(state.total_ticks==21 && state.cpu_statistics[1].timer_ticks==2 && state.cpu_statistics[1].user_ticks==2);
  assert(current->consumed_ticks==2 && state.secondary_cpus[0].preempt_requested);
  simulated_cpu=0;scheduler_handle_timer_tick();assert(state.total_ticks==22);
  assert(state.cpu_statistics[0].timer_ticks==1 && state.blocked_thread_count==1);
  g_active_scheduler=nullptr;
}
int main() {
  fifo_and_no_notification();priorities_and_kernel_affinity();balance_and_pin();
  circular_fifo_stress();deadlines_and_local_ticks();
  std::puts("scheduler real helpers: cross-CPU FIFO, no duplicate/IPI rotation, priority/pin, wrap/capacity, PIT deadline offset/expiry/overflow, wake and local-vs-wall ticks passed");
}
