#include "net/network.hpp"
#include "cpu/smp.hpp"
#include "interrupts/interrupts.hpp"
#include "interrupts/pit.hpp"
#include "runtime/runtime.hpp"
#include "task/scheduler.hpp"

namespace {
constexpr uint16_t kEthernetIpv4 = 0x0800;
constexpr uint16_t kEthernetArp = 0x0806;
constexpr size_t kEthernetHeader = 14;
constexpr size_t kIpv4Header = 20;
constexpr size_t kUdpHeader = 8;
constexpr uint16_t kArpEntries = 8;
constexpr uint16_t kSockets = 4;
constexpr uint16_t kDatagrams = 4;
constexpr uint16_t kPingIdentifier = 0x6400;
constexpr size_t kPingPayload = 32;
struct ArpEntry { bool valid; uint32_t address; uint8_t mac[6]; uint64_t learned; };
struct Datagram { NetworkDatagram metadata; uint8_t bytes[kNetworkUdpMaxPayload]; };
struct UdpWaiter { ThreadControlBlock* thread; uint32_t tid; };
struct UdpSocket {
  bool open;
  uint16_t port;
  uint32_t generation;
  uint32_t owner_pid;
  uint16_t first;
  uint16_t count;
  Datagram inbox[kDatagrams];
  UdpWaiter waiters[kSchedulerMaxThreadCount];
};
struct PingState {
  bool active;
  bool replied;
  uint32_t peer;
  uint16_t sequence;
  uint64_t started;
  uint64_t completed;
};
struct State {
  NetworkStatus status;
  ArpEntry arp[kArpEntries];
  UdpSocket sockets[kSockets];
  PingState ping;
  uint16_t next_sequence;
  uint16_t next_identification;
  bool polling;
};
State g_network;
// SMP 下整个内核持 CPU-owned BKL；它提供跨 CPU 互斥，cli 只保护本 CPU 的
// “检查条件→登记等待→block”时序。不能把 cli/volatile 当成跨核锁。
void restore_interrupts(bool enabled) { if(enabled) enable_interrupts(); }
void wake_udp_receivers(UdpSocket* socket,bool all) {
  for(auto& waiter:socket->waiters) {
    ThreadControlBlock* thread=waiter.thread;
    const uint32_t tid=waiter.tid;
    waiter={};
    // 超时/退出后的旧 TCB 指针不能唤醒恰好复用了同一槽的新线程。
    if(thread && thread->in_use && thread->tid==tid && scheduler_wake_thread(thread)) {
      ++g_network.status.udp_wait_wakes;
      if(!all) return;
    }
  }
}
uint16_t read16(const uint8_t* bytes) { return static_cast<uint16_t>((uint16_t{bytes[0]}<<8)|bytes[1]); }
uint32_t read32(const uint8_t* bytes) { return (uint32_t{bytes[0]}<<24)|(uint32_t{bytes[1]}<<16)|(uint32_t{bytes[2]}<<8)|bytes[3]; }
void write16(uint8_t* bytes,uint16_t value) { bytes[0]=static_cast<uint8_t>(value>>8); bytes[1]=static_cast<uint8_t>(value); }
void write32(uint8_t* bytes,uint32_t value) {
  bytes[0]=static_cast<uint8_t>(value>>24); bytes[1]=static_cast<uint8_t>(value>>16);
  bytes[2]=static_cast<uint8_t>(value>>8); bytes[3]=static_cast<uint8_t>(value);
}
bool equal_mac(const uint8_t* a,const uint8_t* b) {
  for(unsigned index=0;index<6;++index) if(a[index]!=b[index]) return false;
  return true;
}
bool broadcast_mac(const uint8_t* mac) {
  for(unsigned index=0;index<6;++index) if(mac[index]!=0xff) return false;
  return true;
}
bool unicast_mac(const uint8_t* mac) {
  if((mac[0]&1)!=0) return false;
  for(unsigned index=0;index<6;++index) if(mac[index]!=0) return true;
  return false;
}
uint32_t checksum_sum(const uint8_t* bytes,size_t size,uint32_t sum=0) {
  while(size>=2) { sum+=read16(bytes); bytes+=2; size-=2; }
  if(size) sum+=uint16_t{bytes[0]}<<8; // 奇数尾字节放在 16 位字的高 8 位，低 8 位补零。
  return sum;
}
uint16_t checksum_finish(uint32_t sum) {
  while(sum>>16) sum=(sum&0xffff)+(sum>>16);
  return static_cast<uint16_t>(~sum);
}
uint16_t checksum(const uint8_t* bytes,size_t size) { return checksum_finish(checksum_sum(bytes,size)); }
uint16_t udp_checksum(uint32_t source,uint32_t destination,const uint8_t* bytes,size_t size) {
  // UDP checksum 还要覆盖 IP 地址与协议号，防止包被送到错误端点。
  uint32_t sum=(source>>16)+(source&0xffff)+(destination>>16)+(destination&0xffff)+17+
               static_cast<uint32_t>(size);
  return checksum_finish(checksum_sum(bytes,size,sum));
}
uint64_t timeout_ticks(uint32_t milliseconds) {
  return (static_cast<uint64_t>(milliseconds)*timer_frequency_hz()+999)/1000;
}
bool valid_destination(uint32_t address) {
  if(address==0 || address==0xffffffff || (address>>24)>=224) return false;
  const uint32_t mask=g_network.status.netmask;
  return (address&mask)!=(g_network.status.address&mask) ||
         (address&~mask)!=~mask;
}
uint32_t next_hop(uint32_t destination) {
  return (destination&g_network.status.netmask)==(g_network.status.address&g_network.status.netmask)
      ? destination:g_network.status.gateway;
}
void learn_arp(uint32_t address,const uint8_t* mac) {
  if(!valid_destination(address) || address==g_network.status.address || !unicast_mac(mac)) return;
  ArpEntry* target=nullptr;
  for(auto& entry:g_network.arp) {
    if(entry.valid && entry.address==address) { target=&entry; break; }
    if(!entry.valid && target==nullptr) target=&entry;
  }
  if(target==nullptr) {
    target=&g_network.arp[0];
    for(auto& entry:g_network.arp) if(entry.learned<target->learned) target=&entry;
  }
  target->valid=true; target->address=address; target->learned=timer_tick_count();
  memory_copy(target->mac,mac,6);
}
const uint8_t* find_arp(uint32_t address) {
  const uint64_t now=timer_tick_count();
  const uint64_t lifetime=static_cast<uint64_t>(timer_frequency_hz())*60;
  for(auto& entry:g_network.arp) {
    if(entry.valid && lifetime!=0 && now-entry.learned>=lifetime) entry.valid=false;
    if(entry.valid && entry.address==address) return entry.mac;
  }
  return nullptr;
}
void ethernet_header(uint8_t* frame,const uint8_t* destination,uint16_t type) {
  memory_copy(frame,destination,6);
  memory_copy(frame+6,virtio_net_status().mac,6);
  write16(frame+12,type);
}
bool transmit_frame(uint8_t* frame,size_t bytes) {
  // 以太网不含 FCS 的最小帧长为 60 字节；填充不是 IP/UDP 的有效载荷。
  if(bytes<60) { memory_set(frame+bytes,0,60-bytes); bytes=60; }
  const bool sent=virtio_net_send(frame,bytes);
  // TX completion 校验可能发现设备错误；不能让无限等待的接收者一直睡着。
  if(!virtio_net_status().ready) (void)network_status();
  return sent;
}
bool send_arp(uint16_t operation,uint32_t destination_ip,const uint8_t* destination_mac) {
  uint8_t frame[60]{};
  ethernet_header(frame,destination_mac,kEthernetArp);
  uint8_t* arp=frame+kEthernetHeader;
  write16(arp,1); write16(arp+2,kEthernetIpv4); arp[4]=6; arp[5]=4; write16(arp+6,operation);
  memory_copy(arp+8,virtio_net_status().mac,6); write32(arp+14,g_network.status.address);
  if(operation==2) memory_copy(arp+18,destination_mac,6);
  write32(arp+24,destination_ip);
  if(!transmit_frame(frame,42)) return false;
  if(operation==1) ++g_network.status.arp_requests; else ++g_network.status.arp_replies;
  return true;
}
bool resolve_mac(uint32_t destination,uint32_t timeout_ms,uint8_t* result) {
  const uint32_t hop=next_hop(destination);
  const uint8_t* known=find_arp(hop);
  if(known!=nullptr) { memory_copy(result,known,6); return true; }
  if(timeout_ms==0 || timeout_ms>60000 || !timer_is_ready() || !interrupts_are_enabled()) return false;
  const uint8_t broadcast[6]={0xff,0xff,0xff,0xff,0xff,0xff};
  const uint64_t started=timer_tick_count(), ticks=timeout_ticks(timeout_ms);
  uint64_t last_request=started;
  uint32_t requests=1;
  if(!send_arp(1,hop,broadcast)) return false;
  while(timer_tick_count()-started<ticks && network_status().ready) {
    network_poll();
    known=find_arp(hop);
    if(known!=nullptr) { memory_copy(result,known,6); return true; }
    if(requests<3 && timer_tick_count()-last_request>=ticks/3) {
      (void)send_arp(1,hop,broadcast); last_request=timer_tick_count(); ++requests;
    }
    // AP 持 BKL 时不能退回 HLT：BSP 的 tick/网卡 worker 都需要同一把锁。
    // 有线程则必须成功切到其他线程/idle；无法 sleep 时按失败返回。
    if(!timer_sleep_ms(1)) return false;
  }
  return false;
}
bool send_ipv4(const uint8_t* destination_mac,uint32_t destination,uint8_t protocol,
                const uint8_t* payload,size_t size) {
  if(size>1500-kIpv4Header || (size!=0 && payload==nullptr)) return false;
  uint8_t frame[kNetworkEthernetMaxFrame];
  ethernet_header(frame,destination_mac,kEthernetIpv4);
  uint8_t* ip=frame+kEthernetHeader;
  memory_set(ip,0,kIpv4Header);
  ip[0]=0x45; write16(ip+2,static_cast<uint16_t>(kIpv4Header+size));
  write16(ip+4,++g_network.next_identification); write16(ip+6,0x4000); // 不分片。
  ip[8]=64; ip[9]=protocol; write32(ip+12,g_network.status.address); write32(ip+16,destination);
  write16(ip+10,checksum(ip,kIpv4Header));
  if(size) memory_copy(ip+kIpv4Header,payload,size);
  return transmit_frame(frame,kEthernetHeader+kIpv4Header+size);
}
bool send_udp(const uint8_t* destination_mac,uint32_t destination,uint16_t source_port,
               uint16_t destination_port,const void* payload,size_t bytes) {
  if(destination_port==0 || bytes>kNetworkUdpMaxPayload || (bytes && payload==nullptr)) return false;
  uint8_t packet[kUdpHeader+kNetworkUdpMaxPayload];
  write16(packet,source_port); write16(packet+2,destination_port);
  write16(packet+4,static_cast<uint16_t>(kUdpHeader+bytes)); write16(packet+6,0);
  if(bytes) memory_copy(packet+kUdpHeader,payload,bytes);
  uint16_t sum=udp_checksum(g_network.status.address,destination,packet,kUdpHeader+bytes);
  write16(packet+6,sum==0?0xffff:sum);
  if(!send_ipv4(destination_mac,destination,17,packet,kUdpHeader+bytes)) return false;
  ++g_network.status.udp_sent;
  return true;
}
void receive_arp(const uint8_t* frame,size_t bytes) {
  if(bytes<42) { ++g_network.status.invalid_packets; return; }
  const uint8_t* arp=frame+kEthernetHeader;
  const uint16_t operation=read16(arp+6);
  if(read16(arp)!=1 || read16(arp+2)!=kEthernetIpv4 || arp[4]!=6 || arp[5]!=4 ||
      (operation!=1 && operation!=2) || !equal_mac(arp+8,frame+6)) {
    ++g_network.status.invalid_packets; return;
  }
  const uint32_t source=read32(arp+14), destination=read32(arp+24);
  if(destination!=g_network.status.address) return;
  if(operation==2 && !equal_mac(arp+18,virtio_net_status().mac)) {
    ++g_network.status.invalid_packets; return;
  }
  learn_arp(source,arp+8);
  if(operation==1) (void)send_arp(2,source,arp+8);
}
void receive_icmp(const uint8_t* ethernet,uint32_t source,const uint8_t* payload,size_t size) {
  if(size<8 || checksum(payload,size)!=0) { ++g_network.status.invalid_packets; return; }
  if(payload[0]==8 && payload[1]==0) {
    ++g_network.status.icmp_requests;
    uint8_t reply[1500-kIpv4Header];
    memory_copy(reply,payload,size); reply[0]=0; write16(reply+2,0); write16(reply+2,checksum(reply,size));
    if(send_ipv4(ethernet+6,source,1,reply,size)) ++g_network.status.icmp_replies;
  } else if(payload[0]==0 && payload[1]==0 && g_network.ping.active &&
            source==g_network.ping.peer && read16(payload+4)==kPingIdentifier &&
            read16(payload+6)==g_network.ping.sequence && size==8+kPingPayload) {
    for(size_t index=0;index<kPingPayload;++index)
      if(payload[8+index]!=static_cast<uint8_t>(index^g_network.ping.sequence)) {
        ++g_network.status.invalid_packets; return;
      }
    g_network.ping.replied=true; g_network.ping.completed=timer_tick_count();
  } else ++g_network.status.unsupported_packets;
}
void receive_udp(const uint8_t* ethernet,uint32_t source,const uint8_t* payload,size_t size) {
  if(size<kUdpHeader) { ++g_network.status.invalid_packets; return; }
  const uint16_t length=read16(payload+4), source_port=read16(payload), destination_port=read16(payload+2);
  if(length!=size || length<kUdpHeader || destination_port==0 ||
      (read16(payload+6)!=0 && udp_checksum(source,g_network.status.address,payload,size)!=0)) {
    ++g_network.status.invalid_packets; return;
  }
  const size_t bytes=size-kUdpHeader;
  if(bytes>kNetworkUdpMaxPayload) { ++g_network.status.udp_dropped; return; }
  ++g_network.status.udp_received;
  if(destination_port==kNetworkEchoPort) {
    if(send_udp(ethernet+6,source,kNetworkEchoPort,source_port,payload+kUdpHeader,bytes))
      ++g_network.status.udp_echoed;
    else ++g_network.status.udp_dropped;
    return;
  }
  for(auto& socket:g_network.sockets) {
    if(!socket.open || socket.port!=destination_port) continue;
    if(socket.count==kDatagrams) { ++g_network.status.udp_dropped; return; }
    Datagram& datagram=socket.inbox[(socket.first+socket.count)%kDatagrams];
    datagram.metadata={source,source_port,destination_port,static_cast<uint16_t>(bytes),0};
    if(bytes) memory_copy(datagram.bytes,payload+kUdpHeader,bytes);
    ++socket.count;
    wake_udp_receivers(&socket,false);
    return;
  }
  ++g_network.status.udp_dropped;
}
void receive_frame(const uint8_t* frame,size_t bytes,void*) {
  if(frame==nullptr || bytes<kEthernetHeader || bytes>kNetworkEthernetMaxFrame ||
      (!equal_mac(frame,virtio_net_status().mac) && !broadcast_mac(frame)) || !unicast_mac(frame+6)) {
    ++g_network.status.invalid_packets; return;
  }
  const uint16_t type=read16(frame+12);
  if(type==kEthernetArp) { receive_arp(frame,bytes); return; }
  if(type!=kEthernetIpv4) { ++g_network.status.unsupported_packets; return; }
  if(bytes<kEthernetHeader+kIpv4Header) { ++g_network.status.invalid_packets; return; }
  const uint8_t* ip=frame+kEthernetHeader;
  const uint16_t total=read16(ip+2);
  if((ip[0]>>4)!=4 || (ip[0]&15)<5 || total<kIpv4Header || total>bytes-kEthernetHeader) {
    ++g_network.status.invalid_packets; return;
  }
  if(ip[0]!=0x45 || (read16(ip+6)&0xbfff)!=0) {
    ++g_network.status.unsupported_packets; return; // IPv4 options 与分片留给后续章节。
  }
  if(ip[8]==0 || checksum(ip,kIpv4Header)!=0) { ++g_network.status.invalid_packets; return; }
  const uint32_t source=read32(ip+12), destination=read32(ip+16);
  if(destination!=g_network.status.address || !valid_destination(source)) return;
  const uint8_t* payload=ip+kIpv4Header;
  const size_t length=total-kIpv4Header;
  if(ip[9]==1) receive_icmp(frame,source,payload,length);
  else if(ip[9]==17) receive_udp(frame,source,payload,length);
  else ++g_network.status.unsupported_packets;
}
UdpSocket* lookup_socket(int32_t handle,uint32_t owner_pid) {
  if(handle<=0) return nullptr;
  const uint32_t slot=static_cast<uint32_t>(handle)&7;
  if(slot==0 || slot>kSockets) return nullptr;
  UdpSocket* socket=&g_network.sockets[slot-1];
  return socket->open && socket->owner_pid==owner_pid &&
      socket->generation==(static_cast<uint32_t>(handle)>>3) ? socket:nullptr;
}
}

bool network_initialize(PageAllocator* allocator) {
  if(g_network.status.ready) return virtio_net_status().ready;
  if(!virtio_net_initialize(allocator)) return false;
  g_network.status.ready=true;
  g_network.status.address=network_ipv4(10,0,2,15);
  g_network.status.netmask=network_ipv4(255,255,255,0);
  g_network.status.gateway=network_ipv4(10,0,2,2);
  g_network.status.echo_port=kNetworkEchoPort;
  return true;
}
const NetworkStatus& network_status() {
  if(g_network.status.ready && !virtio_net_status().ready) {
    g_network.status.ready=false;
    const bool enabled=interrupts_are_enabled();
    disable_interrupts();
    for(auto& socket:g_network.sockets) wake_udp_receivers(&socket,true);
    restore_interrupts(enabled);
  }
  return g_network.status;
}
uint32_t network_poll(uint32_t budget) {
  // 网卡与硬件 IRQ 留在 BSP；AP 的 UDP syscall 通过共享 inbox 收数据。
  // 尚未启 SMP 的启动/宿主测试仍可主动 pump 协议层。
  if(smp_is_enabled() && smp_current_cpu_index()!=0) return 0;
  if(!network_status().ready || g_network.polling) return 0;
  g_network.polling=true;
  const uint32_t count=virtio_net_poll(budget,receive_frame,nullptr);
  g_network.polling=false;
  (void)network_status();
  return count;
}
bool network_parse_ipv4(const char* text,uint32_t* address) {
  if(text==nullptr || address==nullptr) return false;
  uint32_t value=0;
  for(unsigned part=0;part<4;++part) {
    if(*text<'0' || *text>'9') return false;
    uint32_t byte=0;
    unsigned digits=0;
    while(*text>='0' && *text<='9') {
      byte=byte*10+static_cast<unsigned>(*text++-'0');
      if(++digits>3 || byte>255) return false;
    }
    value=(value<<8)|byte;
    if(part<3) { if(*text++!='.') return false; } else if(*text!='\0') return false;
  }
  *address=value; return true;
}
bool network_format_ipv4(uint32_t address,char* output,size_t capacity) {
  if(output==nullptr || capacity<16) return false;
  size_t position=0;
  for(unsigned part=0;part<4;++part) {
    const uint8_t value=static_cast<uint8_t>(address>>(24-part*8));
    if(value>=100) output[position++]=static_cast<char>('0'+value/100);
    if(value>=10) output[position++]=static_cast<char>('0'+(value/10)%10);
    output[position++]=static_cast<char>('0'+value%10);
    if(part<3) output[position++]='.';
  }
  output[position]='\0'; return true;
}
bool network_ping(uint32_t destination,uint32_t timeout_ms,NetworkPingResult* result) {
  if(result==nullptr) return false;
  memory_set(result,0,sizeof(*result));
  if(!network_status().ready || !valid_destination(destination) || timeout_ms==0 || timeout_ms>60000 ||
      !timer_is_ready() || !interrupts_are_enabled() || g_network.ping.active) return false;
  uint8_t mac[6];
  // ARP 等待会主动调度其它线程，先占用 ping 状态，避免两个调用互相覆盖。
  g_network.ping.active=true;
  if(!resolve_mac(destination,timeout_ms,mac)) { g_network.ping.active=false; return false; }
  uint8_t packet[8+kPingPayload]{};
  const uint16_t sequence=++g_network.next_sequence;
  packet[0]=8; write16(packet+4,kPingIdentifier); write16(packet+6,sequence);
  for(size_t index=0;index<kPingPayload;++index) packet[8+index]=static_cast<uint8_t>(index^sequence);
  write16(packet+2,checksum(packet,sizeof(packet)));
  g_network.ping={true,false,destination,sequence,timer_tick_count(),0};
  result->sequence=sequence;
  result->sent=send_ipv4(mac,destination,1,packet,sizeof(packet));
  if(!result->sent) { g_network.ping.active=false; return false; }
  const uint64_t ticks=timeout_ticks(timeout_ms);
  while(timer_tick_count()-g_network.ping.started<ticks && !g_network.ping.replied && network_status().ready) {
    network_poll();
    if(!g_network.ping.replied && !timer_sleep_ms(1)) break;
  }
  result->replied=g_network.ping.replied;
  if(result->replied) result->round_trip_ms=static_cast<uint32_t>(
      (g_network.ping.completed-g_network.ping.started)*1000/timer_frequency_hz());
  g_network.ping.active=false;
  return result->replied;
}
int32_t network_udp_open(uint16_t port,uint32_t owner_pid) {
  if(!network_status().ready) return -3;
  if(port==0 || port==kNetworkEchoPort) return -2;
  for(const auto& socket:g_network.sockets) if(socket.open && socket.port==port) return -2;
  for(uint32_t index=0;index<kSockets;++index) {
    UdpSocket& socket=g_network.sockets[index];
    if(socket.open) continue;
    socket.generation=socket.generation==0x0fffffff?1:socket.generation+1;
    socket.open=true; socket.port=port; socket.owner_pid=owner_pid; socket.first=socket.count=0;
    return static_cast<int32_t>((socket.generation<<3)|(index+1));
  }
  return -1;
}
bool network_udp_close(int32_t handle,uint32_t owner_pid) {
  const bool enabled=interrupts_are_enabled();
  disable_interrupts();
  UdpSocket* socket=lookup_socket(handle,owner_pid);
  if(socket==nullptr) { restore_interrupts(enabled); return false; }
  socket->open=false; socket->first=socket->count=0;
  wake_udp_receivers(socket,true);
  restore_interrupts(enabled);
  return true;
}
void network_udp_close_owner(uint32_t owner_pid) {
  const bool enabled=interrupts_are_enabled();
  disable_interrupts();
  for(auto& socket:g_network.sockets)
    if(socket.open && socket.owner_pid==owner_pid) {
      socket.open=false; socket.first=socket.count=0; wake_udp_receivers(&socket,true);
    }
  restore_interrupts(enabled);
}
int32_t network_udp_send(int32_t handle,uint32_t destination,uint16_t port,const void* payload,
                         size_t bytes,uint32_t arp_timeout_ms,uint32_t owner_pid) {
  if(!network_status().ready) return -3;
  UdpSocket* socket=lookup_socket(handle,owner_pid);
  if(socket==nullptr || !valid_destination(destination) || port==0 || bytes>kNetworkUdpMaxPayload ||
      (bytes && payload==nullptr) || arp_timeout_ms>60000) return -2;
  uint8_t mac[6];
  if(!resolve_mac(destination,arp_timeout_ms,mac)) return -1;
  // ARP 等待期间允许调度；旧 handle 若被关闭，不能误用刚好占同一槽的新 socket。
  if(lookup_socket(handle,owner_pid)!=socket) return -2;
  if(!send_udp(mac,destination,socket->port,port,payload,bytes)) return -1;
  return static_cast<int32_t>(bytes);
}
int32_t network_udp_receive(int32_t handle,NetworkDatagram* metadata,void* payload,size_t capacity,
                            uint32_t owner_pid) {
  if(!network_status().ready) return -3;
  UdpSocket* socket=lookup_socket(handle,owner_pid);
  if(socket==nullptr || metadata==nullptr) return -2;
  network_poll();
  if(socket->count==0) return -1;
  const Datagram& datagram=socket->inbox[socket->first];
  const uint16_t bytes=datagram.metadata.payload_bytes;
  if(capacity<bytes || (bytes && payload==nullptr)) return -2;
  *metadata=datagram.metadata;
  if(bytes) memory_copy(payload,datagram.bytes,bytes);
  socket->first=static_cast<uint16_t>((socket->first+1)%kDatagrams);
  --socket->count;
  return bytes;
}

int32_t network_udp_receive_wait(int32_t handle,NetworkDatagram* metadata,void* payload,
                                 size_t capacity,uint32_t timeout_ms,uint32_t owner_pid) {
  ++g_network.status.udp_wait_calls;
  if(!network_status().ready) return -3;
  if(metadata==nullptr || capacity>kNetworkUdpMaxPayload || (capacity && payload==nullptr) ||
      (timeout_ms>60000 && timeout_ms!=UINT32_MAX)) return -2;
  UdpSocket* socket=lookup_socket(handle,owner_pid);
  if(socket==nullptr) return -2;
  ThreadControlBlock* const thread=scheduler_active_thread();
  uint64_t deadline=UINT64_MAX;
  if(timeout_ms!=0 && timeout_ms!=UINT32_MAX) {
    if(!timer_is_ready()) return -2;
    const uint64_t now=timer_tick_count(), ticks=timeout_ticks(timeout_ms);
    if(ticks==0 || now>=UINT64_MAX-ticks) return -2;
    deadline=now+ticks;
  }
  bool waited=false;
  for(;;) {
    const bool enabled=interrupts_are_enabled();
    disable_interrupts();
    if(!network_status().ready) { restore_interrupts(enabled); return -3; }
    socket=lookup_socket(handle,owner_pid);
    if(socket==nullptr) {
      restore_interrupts(enabled);
      if(waited) { ++g_network.status.udp_wait_closed; return -4; }
      return -2;
    }
    if(socket->count) {
      // 线程可能在等待时被调度出去，用户堆映射也可能被另一个线程修改。
      // 不能仅相信进入 syscall 时的检查；最终 copy 前按当前地址空间重验。
      if(thread && thread->execution_mode==kThreadExecutionModeUser &&
          (!scheduler_user_range_valid(reinterpret_cast<uint64_t>(metadata),sizeof(*metadata),true) ||
           (capacity && !scheduler_user_range_valid(reinterpret_cast<uint64_t>(payload),capacity,true)))) {
        restore_interrupts(enabled); return -2;
      }
      const Datagram& datagram=socket->inbox[socket->first];
      const uint16_t bytes=datagram.metadata.payload_bytes;
      if(capacity<bytes) { restore_interrupts(enabled); return -2; }
      *metadata=datagram.metadata;
      if(bytes) memory_copy(payload,datagram.bytes,bytes);
      socket->first=static_cast<uint16_t>((socket->first+1)%kDatagrams);
      --socket->count;
      restore_interrupts(enabled);
      return bytes;
    }
    if(timeout_ms==0 || (deadline!=UINT64_MAX && timer_tick_count()>=deadline)) {
      if(timeout_ms) ++g_network.status.udp_wait_timeouts;
      restore_interrupts(enabled); return -1;
    }
    if(!enabled || !thread || thread->is_idle_thread || !timer_is_ready()) {
      restore_interrupts(enabled); return -2;
    }
    size_t waiter_slot=0;
    while(waiter_slot<kSchedulerMaxThreadCount && socket->waiters[waiter_slot].thread &&
          socket->waiters[waiter_slot].thread!=thread) ++waiter_slot;
    if(waiter_slot==kSchedulerMaxThreadCount) { restore_interrupts(enabled); return -1; }
    socket->waiters[waiter_slot]={thread,thread->tid};
    waited=true;
    // BKL 覆盖不同 CPU，cli 覆盖本 CPU：收包方不可能在登记与 block 之间
    // 抢跑并漏掉唤醒。截止时间由统一 BSP tick 驱动，AP tick 不重复计时。
    const bool blocked=scheduler_block_current_thread_until(deadline);
    disable_interrupts();
    if(socket->waiters[waiter_slot].thread==thread && socket->waiters[waiter_slot].tid==thread->tid)
      socket->waiters[waiter_slot]={};
    restore_interrupts(enabled);
    if(blocked) { ++g_network.status.udp_wait_blocks; continue; }
    // 过期检查与调度器登记之间恰好到 deadline 时，不 busy-loop；重验一次
    // 条件可优先取已到达的数据，再返回 timeout。无可用调度器则直接失败。
    if(deadline!=UINT64_MAX && timer_tick_count()>=deadline) continue;
    return -1;
  }
}
