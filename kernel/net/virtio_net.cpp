#include "net/virtio_net.hpp"
#include "device/pci.hpp"
#include "memory/paging.hpp"
#include "runtime/runtime.hpp"

namespace {
constexpr uint16_t kMaxRxBuffers = 64;
constexpr uint16_t kMaxTxBuffers = 64;
constexpr uint16_t kMaxQueueSize = 256;
constexpr uint16_t kMaxRingPages = 3;
constexpr uint16_t kHeaderBytes = 10;
constexpr uint16_t kPayloadOffset = 64;
constexpr uint16_t kDescriptorNext = 1;
constexpr uint16_t kDescriptorWrite = 2;
struct Descriptor { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; };
struct UsedElement { uint32_t id; uint32_t length; };
static_assert(sizeof(Descriptor) == 16, "virtio descriptor layout");
static_assert(sizeof(UsedElement) == 8, "virtio used layout");
struct Queue {
  uint16_t size;
  uint16_t next_available;
  uint16_t last_used;
  uint64_t ring_pages[kMaxRingPages];
  uint16_t ring_page_count;
  volatile Descriptor* descriptors;
  volatile uint16_t* available;
  volatile uint16_t* used_header;
  volatile UsedElement* used;
};
struct Driver {
  VirtioNetStatus status;
  PageAllocator* allocator;
  PciDevice pci;
  Queue rx;
  Queue tx;
  uint64_t rx_pages[kMaxRxBuffers];
  uint64_t tx_pages[kMaxTxBuffers];
  bool tx_in_flight[kMaxTxBuffers];
  volatile bool configuration_pending;
};
Driver g_driver;
uint8_t in8(uint16_t port) { uint8_t v; asm volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
uint16_t in16(uint16_t port) { uint16_t v; asm volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v; }
uint32_t in32(uint16_t port) { uint32_t v; asm volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
void out8(uint16_t port,uint8_t v) { asm volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
void out16(uint16_t port,uint16_t v) { asm volatile("outw %0,%1"::"a"(v),"Nd"(port)); }
void out32(uint16_t port,uint32_t v) { asm volatile("outl %0,%1"::"a"(v),"Nd"(port)); }
void publish_barrier() { asm volatile("mfence" ::: "memory"); }
void acquire_barrier() { asm volatile("lfence" ::: "memory"); }
uint64_t align_page(uint64_t value) { return (value + kPageSize - 1) & ~(kPageSize - 1); }

// legacy queue PFN 只提供起始物理地址，所以整个 ring 必须物理连续。
// C++ 指针连续（例如 kmalloc）并不保证背后的物理页连续，不能直接拿来做 DMA。
bool allocate_ring(Queue* queue, uint16_t id) {
  const uint16_t io = g_driver.status.io_base;
  out16(static_cast<uint16_t>(io + 14), id);
  queue->size = in16(static_cast<uint16_t>(io + 12));
  if (queue->size < 64 || queue->size > kMaxQueueSize ||
      (queue->size & (queue->size - 1)) != 0 || in32(static_cast<uint16_t>(io + 8)) != 0) return false;
  const uint64_t used_offset = align_page(sizeof(Descriptor) * queue->size + 2 * (3 + queue->size));
  const uint64_t bytes = used_offset + align_page(2 * 3 + sizeof(UsedElement) * queue->size);
  const uint16_t count = static_cast<uint16_t>(bytes / kPageSize);
  if (count == 0 || count > kMaxRingPages) return false;
  uint64_t search = kAllocatorMinAddress;
  bool allocated = false;
  for (uint32_t attempt = 0; attempt < kAllocatorPageCount && search < kAllocatorManagedLimit; ++attempt) {
    uint16_t obtained = 0;
    const uint64_t first = alloc_page_at_least(g_driver.allocator, search);
    if (first == 0) break;
    queue->ring_pages[obtained++] = first;
    for (uint16_t index = 1; index < count; ++index) {
      const uint64_t page = alloc_page_at_least(g_driver.allocator, first + index * kPageSize);
      if (page == 0) break;
      queue->ring_pages[obtained++] = page;
      if (page != first + index * kPageSize) break;
    }
    if (obtained == count && queue->ring_pages[count - 1] == first + (count - 1) * kPageSize) {
      allocated = true; break;
    }
    search = first + kPageSize;
    while (obtained != 0) (void)free_page(g_driver.allocator, queue->ring_pages[--obtained]);
  }
  if (!allocated) return false;
  queue->ring_page_count = count;
  auto* bytes_pointer = static_cast<uint8_t*>(paging_physical_pointer(queue->ring_pages[0]));
  if (bytes_pointer == nullptr) return false;
  memory_set(bytes_pointer, 0, bytes);
  queue->descriptors = reinterpret_cast<volatile Descriptor*>(bytes_pointer);
  queue->available = reinterpret_cast<volatile uint16_t*>(bytes_pointer + sizeof(Descriptor) * queue->size);
  queue->used_header = reinterpret_cast<volatile uint16_t*>(bytes_pointer + used_offset);
  queue->used = reinterpret_cast<volatile UsedElement*>(bytes_pointer + used_offset + 4);
  queue->available[0] = 1; // NO_INTERRUPT：本教程通过有界 poll 处理数据。
  out32(static_cast<uint16_t>(io + 8), static_cast<uint32_t>(queue->ring_pages[0] / kPageSize));
  return in32(static_cast<uint16_t>(io + 8)) == queue->ring_pages[0] / kPageSize;
}
void release_pages() {
  Queue* queues[2] = {&g_driver.rx, &g_driver.tx};
  for (Queue* queue : queues) {
    for (uint16_t index = 0; index < queue->ring_page_count; ++index)
      (void)free_page(g_driver.allocator, queue->ring_pages[index]);
  }
  for (uint64_t page : g_driver.rx_pages) if (page) (void)free_page(g_driver.allocator, page);
  for (uint64_t page : g_driver.tx_pages) if (page) (void)free_page(g_driver.allocator, page);
}
void enqueue(Queue* queue, uint16_t head) {
  queue->available[2 + queue->next_available % queue->size] = head;
  ++queue->next_available; // 16 位按规格自然回绕；与 used index 作模 65536 比较。
}
void notify(Queue* queue, uint16_t id) {
  // 描述符、缓冲区、ring slot 必须先对设备可见，最后才能公开新的 index。
  publish_barrier();
  queue->available[1] = queue->next_available;
  publish_barrier();
  out16(static_cast<uint16_t>(g_driver.status.io_base + 16), id);
}
void configure_chain(Queue* queue, uint16_t head, uint16_t next,
                     uint64_t page, uint32_t payload_length, bool writable) {
  // 未协商 ANY_LAYOUT 时，10 字节 virtio header 必须用独立 descriptor。
  queue->descriptors[head].address = page;
  queue->descriptors[head].length = kHeaderBytes;
  queue->descriptors[head].flags = kDescriptorNext | (writable ? kDescriptorWrite : 0);
  queue->descriptors[head].next = next;
  queue->descriptors[next].address = page + kPayloadOffset;
  queue->descriptors[next].length = payload_length;
  queue->descriptors[next].flags = writable ? kDescriptorWrite : 0;
  queue->descriptors[next].next = 0;
}
void fail_device() {
  ++g_driver.status.device_errors;
  g_driver.status.ready = false;
  out8(static_cast<uint16_t>(g_driver.status.io_base + 18), 0x87);
  virtio_net_disable_receive_interrupts();
  // 不释放仍可能被设备 DMA 访问的页面。设备错误后停止收发，等待重启。
}
void reap_transmit() {
  Queue* queue = &g_driver.tx;
  const uint16_t end = queue->used_header[1];
  acquire_barrier();
  if (static_cast<uint16_t>(end - queue->last_used) > g_driver.status.tx_buffer_count) { fail_device(); return; }
  while (queue->last_used != end) {
    const uint32_t head = queue->used[queue->last_used % queue->size].id;
    if (head >= g_driver.status.tx_buffer_count || !g_driver.tx_in_flight[head]) { fail_device(); return; }
    g_driver.tx_in_flight[head] = false;
    ++queue->last_used;
  }
}
}

bool virtio_net_initialize(PageAllocator* allocator) {
  if (g_driver.status.ready) return true;
  // 出现 DMA 错误的实例不尝试在原状态上重初始化，避免重复释放仍在使用的页。
  if (allocator == nullptr || g_driver.allocator != nullptr) return false;
  PciDevice device{};
  if (!pci_find_device(0x1af4, 0x1000, &device) || (pci_read32(device, 8) & 0xff) != 0) return false;
  const uint32_t bar = pci_read32(device, 0x10);
  if ((bar & 1) == 0 || (bar & ~3U) == 0 || (bar & ~3U) > 0xffc0) return false;
  g_driver.allocator = allocator;
  g_driver.pci = device;
  g_driver.status.io_base = static_cast<uint16_t>(bar & ~3U);
  const uint16_t io = g_driver.status.io_base;
  const uint32_t interrupt_config = pci_read32(device, 0x3c);
  g_driver.status.irq_line = ((interrupt_config >> 8) & 0xff) >= 1 &&
      ((interrupt_config >> 8) & 0xff) <= 4 ? static_cast<uint8_t>(interrupt_config) : 0xff;
  pci_disable_msix(device);
  const uint16_t command = pci_read16(device, 4);
  pci_write16(device, 4, static_cast<uint16_t>(command | 1 | 4 | 0x400));
  out8(static_cast<uint16_t>(io + 18), 0);
  if (in8(static_cast<uint16_t>(io + 18)) != 0) return false;
  out8(static_cast<uint16_t>(io + 18), 1);
  out8(static_cast<uint16_t>(io + 18), 3);
  const uint32_t features = in32(io);
  if ((features & (1U << 5)) == 0) { out8(static_cast<uint16_t>(io + 18), 0x83); return false; }
  // 只接收 MAC。校验和、GSO、合并接收缓冲区、间接描述符由后续章节再实现。
  out32(static_cast<uint16_t>(io + 4), 1U << 5);
  for (unsigned index = 0; index < 6; ++index) g_driver.status.mac[index] = in8(static_cast<uint16_t>(io + 20 + index));
  bool valid_mac = false;
  for (uint8_t value : g_driver.status.mac) valid_mac = valid_mac || value != 0;
  bool success = valid_mac && (g_driver.status.mac[0] & 1) == 0 &&
                 allocate_ring(&g_driver.rx, 0) && allocate_ring(&g_driver.tx, 1);
  // 每个帧占两个 descriptor：一个 virtio header，一个 Ethernet 数据区。
  // queue=64 时只使用 32 个槽；QEMU 的 queue=256 时使用 64 个槽。
  // TX 不能比一次 RX poll 的 32 包预算还小，否则同批回显会被自身缓冲上限丢弃。
  g_driver.status.rx_buffer_count = g_driver.rx.size / 2 < kMaxRxBuffers ? g_driver.rx.size / 2 : kMaxRxBuffers;
  g_driver.status.tx_buffer_count = g_driver.tx.size / 2 < kMaxTxBuffers ? g_driver.tx.size / 2 : kMaxTxBuffers;
  for (uint16_t index = 0; success && index < g_driver.status.rx_buffer_count; ++index) {
    const uint64_t page = alloc_page(allocator);
    g_driver.rx_pages[index] = page;
    auto* pointer = static_cast<uint8_t*>(paging_physical_pointer(page));
    if (page == 0 || pointer == nullptr) { success = false; break; }
    memory_set(pointer, 0, kPageSize);
    configure_chain(&g_driver.rx, index, static_cast<uint16_t>(index + g_driver.status.rx_buffer_count), page, kNetworkEthernetMaxFrame, true);
    enqueue(&g_driver.rx, index);
  }
  for (uint16_t index = 0; success && index < g_driver.status.tx_buffer_count; ++index) {
    const uint64_t page = alloc_page(allocator);
    g_driver.tx_pages[index] = page;
    auto* pointer = paging_physical_pointer(page);
    if (page == 0 || pointer == nullptr) { success = false; break; }
    memory_set(pointer, 0, kPageSize);
  }
  if (!success) {
    out8(static_cast<uint16_t>(io + 18), 0);
    if (in8(static_cast<uint16_t>(io + 18)) == 0) release_pages();
    return false;
  }
  g_driver.status.rx_queue_size = g_driver.rx.size;
  g_driver.status.tx_queue_size = g_driver.tx.size;
  out8(static_cast<uint16_t>(io + 18), 7);
  g_driver.status.ready = (in8(static_cast<uint16_t>(io + 18)) & 7) == 7;
  if (g_driver.status.ready) notify(&g_driver.rx, 0);
  return g_driver.status.ready;
}
const VirtioNetStatus& virtio_net_status() { return g_driver.status; }
uint32_t virtio_net_poll(uint32_t budget, EthernetReceiveHandler handler, void* context) {
  if (!g_driver.status.ready || handler == nullptr || budget == 0) return 0;
  if (budget > g_driver.status.rx_buffer_count) budget = g_driver.status.rx_buffer_count;
  reap_transmit();
  if (!g_driver.status.ready) return 0;
  if (!g_driver.status.irq_enabled) (void)in8(static_cast<uint16_t>(g_driver.status.io_base + 19));
  if (g_driver.configuration_pending) {
    g_driver.configuration_pending = false;
    uint8_t mac[6]; bool valid = false;
    for (unsigned i = 0; i < 6; ++i) {
      mac[i] = in8(static_cast<uint16_t>(g_driver.status.io_base + 20 + i));
      valid = valid || mac[i] != 0;
    }
    if (!valid || (mac[0] & 1)) { fail_device(); return 0; }
    memory_copy(g_driver.status.mac, mac, 6);
  }
  Queue* queue = &g_driver.rx;
  const uint16_t end = queue->used_header[1];
  acquire_barrier();
  if (static_cast<uint16_t>(end - queue->last_used) > g_driver.status.rx_buffer_count) { fail_device(); return 0; }
  uint32_t processed = 0;
  while (queue->last_used != end && processed < budget) {
    const uint32_t head = queue->used[queue->last_used % queue->size].id;
    const uint32_t length = queue->used[queue->last_used % queue->size].length;
    if (head >= g_driver.status.rx_buffer_count) { fail_device(); return processed; }
    auto* bytes = static_cast<uint8_t*>(paging_physical_pointer(g_driver.rx_pages[head]));
    if (length < kHeaderBytes + 14 || length > kHeaderBytes + kNetworkEthernetMaxFrame ||
        bytes == nullptr || bytes[0] != 0 || bytes[1] != 0) {
      ++g_driver.status.rx_dropped;
    } else {
      ++g_driver.status.rx_packets;
      g_driver.status.rx_bytes += length - kHeaderBytes;
      handler(bytes + kPayloadOffset, length - kHeaderBytes, context);
    }
    ++queue->last_used;
    ++processed;
    enqueue(queue, static_cast<uint16_t>(head));
  }
  if (processed != 0) notify(queue, 0);
  return processed;
}
bool virtio_net_send(const uint8_t* frame, size_t bytes) {
  if (!g_driver.status.ready || frame == nullptr || bytes < 14 || bytes > kNetworkEthernetMaxFrame) return false;
  // 只回收 TX completion，不递归执行 RX callback。回显批处理里的每次 send
  // 都有机会使用刚完成的槽，避免一直等到下一次 10ms poll 才归还发送缓冲区。
  reap_transmit();
  if (!g_driver.status.ready) return false;
  uint16_t slot = g_driver.status.tx_buffer_count;
  for (uint16_t index = 0; index < g_driver.status.tx_buffer_count; ++index) if (!g_driver.tx_in_flight[index]) { slot = index; break; }
  if (slot == g_driver.status.tx_buffer_count) { ++g_driver.status.tx_busy; return false; }
  auto* pointer = static_cast<uint8_t*>(paging_physical_pointer(g_driver.tx_pages[slot]));
  memory_set(pointer, 0, kHeaderBytes);
  memory_copy(pointer + kPayloadOffset, frame, bytes);
  configure_chain(&g_driver.tx, slot, static_cast<uint16_t>(slot + g_driver.status.tx_buffer_count), g_driver.tx_pages[slot], static_cast<uint32_t>(bytes), false);
  g_driver.tx_in_flight[slot] = true;
  enqueue(&g_driver.tx, slot);
  notify(&g_driver.tx, 1);
  ++g_driver.status.tx_packets;
  g_driver.status.tx_bytes += bytes;
  return true;
}

bool virtio_net_enable_receive_interrupts() {
  if (!g_driver.status.ready) return false;
  if (g_driver.status.irq_enabled) return true;
  const uint8_t line = g_driver.status.irq_line;
  // PC 的 ELCR 只允许这些 PIC 输入使用电平触发。PCI INTx 是电平信号，
  // 直接把它接入默认边沿触发 PIC，会在持续收包时错过后续中断。
  constexpr uint16_t kElcrSupportedLines = 0xdef8;
  if (line >= 16 || (kElcrSupportedLines & (1U << line)) == 0) return false;
  const uint16_t elcr = static_cast<uint16_t>(0x4d0 + line / 8);
  out8(elcr, static_cast<uint8_t>(in8(elcr) | (1U << (line % 8))));
  if ((in8(elcr) & (1U << (line % 8))) == 0) return false;
  g_driver.rx.available[0] = 0; // 仅 RX 完成唤醒；TX 在 send/poll 时回收。
  g_driver.tx.available[0] = 1;
  publish_barrier();
  (void)in8(static_cast<uint16_t>(g_driver.status.io_base + 19));
  g_driver.status.irq_enabled = true;
  const uint16_t command = pci_read16(g_driver.pci, 4);
  pci_write16(g_driver.pci, 4, static_cast<uint16_t>(command & ~0x400));
  return true;
}
void virtio_net_disable_receive_interrupts() {
  if (!g_driver.status.irq_enabled) return;
  const uint16_t command = pci_read16(g_driver.pci, 4);
  pci_write16(g_driver.pci, 4, static_cast<uint16_t>(command | 0x400));
  g_driver.rx.available[0] = 1;
  publish_barrier();
  (void)in8(static_cast<uint16_t>(g_driver.status.io_base + 19));
  g_driver.status.irq_enabled = false;
  // 不屏蔽 PIC line：同一路 PCI IRQ 上可能还有其它设备。
}
bool virtio_net_acknowledge_irq(uint8_t irq_line) {
  if (!g_driver.status.irq_enabled || irq_line != g_driver.status.irq_line) return false;
  // read-to-clear：先撤销设备的 INTx 电平，然后调用方才给 PIC 发 EOI。
  // 同 line 但 ISR=0 的 IRQ 属于别的设备，不可把它认领为网卡中断。
  const uint8_t isr = in8(static_cast<uint16_t>(g_driver.status.io_base + 19));
  if ((isr & 3) == 0) return false;
  ++g_driver.status.irq_count;
  if (isr & 2) { ++g_driver.status.configuration_changes; g_driver.configuration_pending = true; }
  acquire_barrier();
  return true;
}
bool virtio_net_receive_pending() {
  if (!g_driver.status.ready) return false;
  const uint16_t end = g_driver.rx.used_header[1];
  acquire_barrier();
  return g_driver.configuration_pending || end != g_driver.rx.last_used;
}
