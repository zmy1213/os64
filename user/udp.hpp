#ifndef OS64_USER_UDP_HPP
#define OS64_USER_UDP_HPP
#include "os64.hpp"

// 与 kernel/net/network.hpp 的数据报头布局相同。IPv4 是 a<<24|b<<16|c<<8|d。
struct UdpDatagram {
  uint32_t source_address;
  uint16_t source_port;
  uint16_t destination_port;
  uint16_t payload_bytes;
};
static_assert(sizeof(UdpDatagram)==12,"UDP metadata ABI includes two tail padding bytes");
inline int32_t udp_open(uint16_t port) { return static_cast<int32_t>(syscall(36,port)); }
inline int64_t udp_close(int32_t handle) { return syscall(37,static_cast<uint32_t>(handle)); }
inline int64_t udp_send(int32_t handle,uint32_t destination,uint16_t port,const void* payload,size_t bytes) {
  return syscall(38,static_cast<uint32_t>(handle),destination,port,reinterpret_cast<uint64_t>(payload),bytes);
}
inline int64_t udp_receive(int32_t handle,UdpDatagram* metadata,void* payload,size_t capacity) {
  return syscall(39,static_cast<uint32_t>(handle),reinterpret_cast<uint64_t>(metadata),
                 reinterpret_cast<uint64_t>(payload),capacity);
}
#endif
