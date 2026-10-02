#ifndef OS64_VIRTIO_NET_HPP
#define OS64_VIRTIO_NET_HPP
#include <stddef.h>
#include <stdint.h>
#include "memory/page_allocator.hpp"

constexpr size_t kNetworkEthernetMaxFrame = 1514;
using EthernetReceiveHandler = void (*)(const uint8_t* frame, size_t bytes, void* context);
struct VirtioNetStatus {
  bool ready;
  uint16_t io_base;
  uint16_t rx_queue_size;
  uint16_t tx_queue_size;
  uint16_t rx_buffer_count;
  uint16_t tx_buffer_count;
  bool irq_enabled;
  uint8_t irq_line;
  uint8_t mac[6];
  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t rx_dropped;
  uint64_t tx_busy;
  uint64_t device_errors;
  uint64_t irq_count;
  uint64_t configuration_changes;
};
bool virtio_net_initialize(PageAllocator* allocator);
const VirtioNetStatus& virtio_net_status();
uint32_t virtio_net_poll(uint32_t budget, EthernetReceiveHandler handler, void* context);
bool virtio_net_send(const uint8_t* frame, size_t bytes);
// legacy INTx/PIC；不在硬中断里解析包。IRQ0/1/2/8/13 不能设为 level trigger。
bool virtio_net_enable_receive_interrupts();
void virtio_net_disable_receive_interrupts();
bool virtio_net_acknowledge_irq(uint8_t irq_line);
bool virtio_net_receive_pending();
#endif
