// 实际 network_irq.cpp 的等待/唤醒协议测试。仅设备寄存器、CPU IF 和调度器是模型。
#include <cstdlib>
#include <iostream>
#include "net/network.hpp"
#include "task/scheduler.hpp"

namespace {
VirtioNetStatus nic{};
ThreadControlBlock worker{};
ThreadControlBlock other{};
ThreadControlBlock* active = &worker;
bool enabled = true, enable_device = true, enable_pic = true;
bool receive_pending = false, own_irq = false, completion_during_park = false;
unsigned blocks, wakes, sleeps, acknowledgements, yields;
void require(bool value, const char* message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
bool interrupts_are_enabled() { return enabled; }
void disable_interrupts() { enabled = false; }
void enable_interrupts() {
  enabled = true;
  if (completion_during_park) {
    completion_during_park = false;
    require(worker.state == kThreadStateBlocked, "IRQ cannot run before blocked registration");
    own_irq = true;
    require(network_handle_irq(11), "completion IRQ belongs to NIC");
  }
}
const VirtioNetStatus& virtio_net_status() { return nic; }
bool virtio_net_enable_receive_interrupts() { nic.irq_enabled = enable_device; return enable_device; }
void virtio_net_disable_receive_interrupts() { nic.irq_enabled = false; }
bool virtio_net_receive_pending() { require(!enabled, "used check must hold cli"); return receive_pending; }
bool virtio_net_acknowledge_irq(uint8_t line) {
  if (!nic.irq_enabled || line != 11 || !own_irq) return false;
  own_irq = false; ++acknowledgements; return true;
}
bool enable_pic_irq(uint8_t line) { require(!enabled && line == 11, "PIC setup uses cli and firmware line"); return enable_pic; }
ThreadControlBlock* scheduler_active_thread() { return active; }
bool scheduler_sleep_current_thread(uint64_t ticks) { require(ticks == 1, "fallback sleeps one tick"); ++sleeps; return true; }
bool scheduler_yield_current_thread() { require(enabled, "batch yields with IRQ enabled"); ++yields; return true; }
bool scheduler_wake_thread(ThreadControlBlock* thread) {
  require(thread == &worker && !own_irq, "device source is cleared before wake");
  ++wakes;
  if (thread->state != kThreadStateBlocked) return false;
  thread->state = kThreadStateReady; return true;
}
bool scheduler_block_current_thread_and_enable_interrupts() {
  require(!enabled && active == &worker, "block registration and used check share cli");
  ++blocks; worker.state = kThreadStateBlocked;
  enable_interrupts();
  require(worker.state == kThreadStateReady, "completion during park wakes registered worker");
  worker.state = kThreadStateRunning;
  return true;
}
int main() {
  worker.in_use = true; worker.execution_mode = kThreadExecutionModeKernel;
  worker.state = kThreadStateRunning;
  nic.ready = true; nic.irq_line = 11;
  require(!network_enable_irq(nullptr), "null worker refused");
  enable_pic = false;
  require(!network_enable_irq(&worker) && !nic.irq_enabled && enabled, "PIC failure rolls back device interrupts");
  network_wait_for_event(); require(sleeps == 1, "disabled IRQ uses poll fallback");
  enable_pic = true;
  require(network_enable_irq(&worker) && enabled, "valid kernel worker enables IRQ");
  require(network_enable_irq(&worker), "same worker setup is idempotent");
  other.in_use = true; other.execution_mode = kThreadExecutionModeKernel;
  require(!network_enable_irq(&other), "cannot replace registered worker");
  own_irq = true;
  require(!network_handle_irq(10) && own_irq, "foreign line leaves source untouched");
  require(network_handle_irq(11) && acknowledgements == 1 && wakes == 1, "owned source acknowledged then wake");
  require(!network_handle_irq(11) && wakes == 1, "shared line with ISR zero is not claimed");
  receive_pending = true;
  network_wait_for_event(); require(blocks == 0 && enabled && yields == 1, "pending RX yields fairly instead of sleeping");
  receive_pending = false; completion_during_park = true;
  network_wait_for_event(); require(blocks == 1 && wakes == 2 && enabled, "completion after check does not lose wakeup");
  active = &other; network_wait_for_event(); require(blocks == 1 && enabled, "other thread cannot park worker");
  active = &worker; enabled = false; network_wait_for_event(); require(!enabled && blocks == 1, "caller IF off is respected");
  std::cout << "Network IRQ host tests passed: shared source, clear-before-wake, pending RX, atomic park race, fallback\n";
}
