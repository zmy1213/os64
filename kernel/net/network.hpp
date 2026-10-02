#ifndef OS64_NETWORK_HPP
#define OS64_NETWORK_HPP
#include <stddef.h>
#include <stdint.h>
#include "memory/page_allocator.hpp"
#include "net/virtio_net.hpp"

constexpr size_t kNetworkUdpMaxPayload = 1200;
constexpr uint16_t kNetworkEchoPort = 9000;
constexpr uint32_t network_ipv4(uint8_t a,uint8_t b,uint8_t c,uint8_t d) {
  return (uint32_t{a}<<24)|(uint32_t{b}<<16)|(uint32_t{c}<<8)|d;
}
struct NetworkStatus {
  bool ready;
  uint32_t address;
  uint32_t netmask;
  uint32_t gateway;
  uint16_t echo_port;
  uint64_t invalid_packets;
  uint64_t unsupported_packets;
  uint64_t arp_requests;
  uint64_t arp_replies;
  uint64_t icmp_requests;
  uint64_t icmp_replies;
  uint64_t udp_received;
  uint64_t udp_sent;
  uint64_t udp_dropped;
  uint64_t udp_echoed;
  uint64_t udp_wait_calls;
  uint64_t udp_wait_blocks;
  uint64_t udp_wait_timeouts;
  uint64_t udp_wait_closed;
  uint64_t udp_wait_wakes;
};
struct NetworkPingResult {
  bool sent;
  bool replied;
  uint16_t sequence;
  uint32_t round_trip_ms; // PIT 的量化时间，不是微秒精度。
};
struct NetworkDatagram {
  uint32_t source_address;
  uint16_t source_port;
  uint16_t destination_port;
  uint16_t payload_bytes;
  uint16_t reserved; // ABI 保留字段，内核收到数据报时显式写 0。
};
static_assert(sizeof(NetworkDatagram)==12,"UDP syscall metadata ABI");
static_assert(offsetof(NetworkDatagram,source_address)==0 && offsetof(NetworkDatagram,source_port)==4 &&
              offsetof(NetworkDatagram,destination_port)==6 && offsetof(NetworkDatagram,payload_bytes)==8 &&
              offsetof(NetworkDatagram,reserved)==10,"UDP metadata field offsets must remain stable");

// 没网卡时返回 false，内核和本地教学实验仍可正常运行。
// 默认静态地址适配 QEMU user network，未实现 DHCP/DNS/TCP/IPv6。
bool network_initialize(PageAllocator* allocator);
const NetworkStatus& network_status();
uint32_t network_poll(uint32_t budget = 32);
struct ThreadControlBlock;
// IRQ 只清设备源并唤醒这个长期存在的内核 worker；解析仍在普通线程。
bool network_enable_irq(ThreadControlBlock* worker);
bool network_handle_irq(uint8_t irq_line);
// worker 的 poll 后调用：原子检查 used ring 再 block，IRQ 不可用则 sleep 一 tick。
void network_wait_for_event();
bool network_parse_ipv4(const char* text,uint32_t* address);
bool network_format_ipv4(uint32_t address,char* output,size_t capacity);
bool network_ping(uint32_t address,uint32_t timeout_ms,NetworkPingResult* result);

// 有界、非阻塞的内核 UDP socket：最多 4 个，每个收件箱最多 4 个包。
// handle 带代数，关闭后的旧 handle 不会误操作新 socket。
// send 返回发送字节数，-1=暂时忙/ARP超时，-2=参数错误，-3=没有网卡。
// receive 返回字节数，-1=收件箱为空，-2=参数错误/缓冲太小，-3=没有网卡。
// 缓冲太小时保留整个包，不悄悄截短；0 字节 UDP 数据报仍可正常收发。
int32_t network_udp_open(uint16_t local_port,uint32_t owner_pid = 0);
bool network_udp_close(int32_t handle,uint32_t owner_pid = 0);
void network_udp_close_owner(uint32_t owner_pid);
int32_t network_udp_send(int32_t handle,uint32_t destination,uint16_t port,
                         const void* payload,size_t bytes,uint32_t arp_timeout_ms = 1000,
                         uint32_t owner_pid = 0);
int32_t network_udp_receive(int32_t handle,NetworkDatagram* metadata,
                            void* payload,size_t capacity,uint32_t owner_pid = 0);
// timeout_ms=0 是立即探测；1..60000 为有限等待；UINT32_MAX 无限等待。
// -1 超时/立即无包，-2 参数错误，-3 无网卡，-4 等待中被关闭。
// 等待不占用 CPU；数据、关闭和全局 BSP 时钟的 deadline 会唤醒线程。
int32_t network_udp_receive_wait(int32_t handle,NetworkDatagram* metadata,
                                void* payload,size_t capacity,uint32_t timeout_ms,
                                uint32_t owner_pid = 0);
#endif
