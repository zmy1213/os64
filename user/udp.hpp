#ifndef OS64_USER_UDP_HPP
#define OS64_USER_UDP_HPP
#include "os64.hpp"

// 与 kernel/net/network.hpp 的数据报头布局相同。IPv4 是 a<<24|b<<16|c<<8|d。
struct UdpDatagram {
  uint32_t source_address;
  uint16_t source_port;
  uint16_t destination_port;
  uint16_t payload_bytes;
  uint16_t reserved; // ABI 保留字段，内核收到数据报时显式写 0。
};
static_assert(sizeof(UdpDatagram)==12,"UDP metadata ABI includes explicit reserved bytes");
static_assert(offsetof(UdpDatagram,source_address)==0 && offsetof(UdpDatagram,source_port)==4 &&
              offsetof(UdpDatagram,destination_port)==6 && offsetof(UdpDatagram,payload_bytes)==8 &&
              offsetof(UdpDatagram,reserved)==10,"UDP metadata field offsets must remain stable");
inline int32_t udp_open(uint16_t port) { return static_cast<int32_t>(syscall(36,port)); }
inline int64_t udp_close(int32_t handle) { return syscall(37,static_cast<uint32_t>(handle)); }
inline int64_t udp_send(int32_t handle,uint32_t destination,uint16_t port,const void* payload,size_t bytes) {
  return syscall(38,static_cast<uint32_t>(handle),destination,port,reinterpret_cast<uint64_t>(payload),bytes);
}
inline int64_t udp_receive(int32_t handle,UdpDatagram* metadata,void* payload,size_t capacity) {
  return syscall(39,static_cast<uint32_t>(handle),reinterpret_cast<uint64_t>(metadata),
                 reinterpret_cast<uint64_t>(payload),capacity);
}
// 0=立即探测，1..60000ms=有限等待，UINT32_MAX=无限等待。
// 接收0字节包返回0；-1超时，-2非法参数，-3无网卡，-4等待时被关闭。
inline int64_t udp_receive_wait(int32_t handle,UdpDatagram* metadata,void* payload,
                                size_t capacity,uint32_t timeout_ms) {
  return syscall(40,static_cast<uint32_t>(handle),reinterpret_cast<uint64_t>(metadata),
                 reinterpret_cast<uint64_t>(payload),capacity,timeout_ms);
}
#endif
