#include "net/network.hpp"
#include "interrupts/interrupts.hpp"
#include "interrupts/pic.hpp"
#include "task/scheduler.hpp"

namespace {
ThreadControlBlock* g_network_worker;
}
bool network_enable_irq(ThreadControlBlock* worker) {
  if (!worker || !worker->in_use || worker->is_idle_thread ||
      worker->execution_mode != kThreadExecutionModeKernel ||
      !virtio_net_status().ready || !interrupts_are_enabled()) return false;
  if (g_network_worker) return worker == g_network_worker && virtio_net_status().irq_enabled;
  disable_interrupts();
  g_network_worker = worker;
  bool enabled = virtio_net_enable_receive_interrupts();
  if (enabled) enabled = enable_pic_irq(virtio_net_status().irq_line);
  if (!enabled) { virtio_net_disable_receive_interrupts(); g_network_worker = nullptr; }
  enable_interrupts();
  return enabled;
}
bool network_handle_irq(uint8_t irq_line) {
  if (!virtio_net_acknowledge_irq(irq_line)) return false;
  // IRQ 中不拷贝包、不分配内存、不切栈。worker 活跃时 wake 返回 false 即可。
  if (g_network_worker) (void)scheduler_wake_thread(g_network_worker);
  return true;
}
void network_wait_for_event() {
  if (!virtio_net_status().irq_enabled || !g_network_worker || !virtio_net_status().ready) {
    (void)scheduler_sleep_current_thread(1);
    return;
  }
  if (scheduler_active_thread() != g_network_worker || !interrupts_are_enabled()) return;
  disable_interrupts();
  // 不能先检查 ring，再开中断，然后才登记 blocked：包可能恰好在这两步之间
  // 完成，IRQ 看见 worker 还在运行，唤醒失败，worker 随后却睡下去，漏掉唯一通知。
  // cli 覆盖“检查→登记 blocked”；调度器登记后才重新开中断。
  if (virtio_net_receive_pending()) {
    enable_interrupts();
    // 网卡持续收包时也不能让一个 ring0 worker 永远占着 CPU。
    // poll 的 32 包预算结束后即使还有数据，也给其它就绪线程一次运行机会。
    (void)scheduler_yield_current_thread();
    return;
  }
  (void)scheduler_block_current_thread_and_enable_interrupts();
}
