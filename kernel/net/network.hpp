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
};
static_assert(sizeof(NetworkDatagram)==12,"UDP syscall metadata ABI");

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
#endif
