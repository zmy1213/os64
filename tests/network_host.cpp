// 协议层接受真实 Ethernet 字节。宿主测试只替换网卡和时钟，不替换解析/校验/UDP代码。
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <random>
#include <vector>
#include "net/network.hpp"

using Packet=std::vector<uint8_t>;
static const uint8_t guest_mac[6]={0x52,0x54,0,0x12,0x34,0x56};
static const uint8_t peer_mac[6]={0x52,0x54,0,0x65,0x43,0x21};
static const uint32_t guest_ip=network_ipv4(10,0,2,15), peer_ip=network_ipv4(10,0,2,2);
static VirtioNetStatus driver{};
static std::deque<Packet> incoming;
static std::vector<Packet> outgoing;
static uint64_t ticks=0;
static bool automatic_answers=true;
static void require(bool value,const char* message) { if(!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);} }
static uint16_t read16(const uint8_t* p) {return static_cast<uint16_t>((p[0]<<8)|p[1]);}
static void put16(uint8_t* p,uint16_t n) {p[0]=n>>8;p[1]=static_cast<uint8_t>(n);}
static void put32(uint8_t* p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=static_cast<uint8_t>(n>>(24-i*8));}
static uint16_t sum(const uint8_t* p,size_t n,uint32_t initial=0) {
  while(n>=2){initial+=read16(p);p+=2;n-=2;}if(n)initial+=p[0]<<8;
  while(initial>>16)initial=(initial&65535)+(initial>>16);
  return static_cast<uint16_t>(~initial);
}
static uint16_t udp_sum(uint32_t a,uint32_t b,const uint8_t* p,size_t n) {
  return sum(p,n,(a>>16)+(a&65535)+(b>>16)+(b&65535)+17+static_cast<uint32_t>(n));
}
static void ether(Packet& frame,uint16_t type) {
  std::memcpy(frame.data(),guest_mac,6);std::memcpy(frame.data()+6,peer_mac,6);put16(frame.data()+12,type);
}
static Packet arp(uint16_t operation,uint32_t source=peer_ip) {
  Packet frame(60);ether(frame,0x806);auto* p=frame.data()+14;
  put16(p,1);put16(p+2,0x800);p[4]=6;p[5]=4;put16(p+6,operation);
  std::memcpy(p+8,peer_mac,6);put32(p+14,source);
  if(operation==2)std::memcpy(p+18,guest_mac,6);
  put32(p+24,guest_ip);return frame;
}
static Packet ipv4(uint8_t protocol,const Packet& payload,uint32_t source=peer_ip) {
  Packet frame(std::max<size_t>(60,34+payload.size()));ether(frame,0x800);auto* ip=frame.data()+14;
  ip[0]=0x45;put16(ip+2,static_cast<uint16_t>(20+payload.size()));put16(ip+6,0x4000);
  ip[8]=64;ip[9]=protocol;put32(ip+12,source);put32(ip+16,guest_ip);put16(ip+10,sum(ip,20));
  std::memcpy(ip+20,payload.data(),payload.size());return frame;
}
static Packet udp(uint16_t port,const Packet& payload,bool checksum=true) {
  Packet data(8+payload.size());put16(data.data(),51000);put16(data.data()+2,port);
  put16(data.data()+4,static_cast<uint16_t>(data.size()));
  if(!payload.empty())std::memcpy(data.data()+8,payload.data(),payload.size());
  if(checksum){uint16_t check=udp_sum(peer_ip,guest_ip,data.data(),data.size());put16(data.data()+6,check?check:65535);}
  return ipv4(17,data);
}
uint64_t timer_tick_count(){return ticks;}
uint32_t timer_frequency_hz(){return 100;}
bool timer_is_ready(){return true;}
bool interrupts_are_enabled(){return true;}
void wait_for_interrupt(){++ticks;}
bool timer_sleep_ms(uint64_t){++ticks;return true;}
bool virtio_net_initialize(PageAllocator*) {driver.ready=true;std::memcpy(driver.mac,guest_mac,6);return true;}
const VirtioNetStatus& virtio_net_status(){return driver;}
uint32_t virtio_net_poll(uint32_t budget,EthernetReceiveHandler handler,void* context) {
  uint32_t processed=0;
  while(processed<budget && !incoming.empty()) {
    Packet packet=std::move(incoming.front());incoming.pop_front();
    handler(packet.data(),packet.size(),context);++processed;
  }
  return processed;
}
bool virtio_net_send(const uint8_t* bytes,size_t length) {
  outgoing.emplace_back(bytes,bytes+length);
  if(!automatic_answers)return true;
  const Packet& frame=outgoing.back();
  if(read16(frame.data()+12)==0x806 && read16(frame.data()+20)==1)incoming.push_back(arp(2));
  else if(read16(frame.data()+12)==0x800 && frame[23]==1 && frame[34]==8) {
    const size_t size=read16(frame.data()+16)-20;
    Packet reply(frame.begin()+34,frame.begin()+34+size);reply[0]=0;put16(reply.data()+2,0);
    put16(reply.data()+2,sum(reply.data(),reply.size()));incoming.push_back(ipv4(1,reply));
  }
  return true;
}
static void deliver(Packet packet){incoming.push_back(std::move(packet));network_poll();}
static void validate_echo(const Packet& payload) {
  outgoing.clear();deliver(udp(kNetworkEchoPort,payload));require(outgoing.size()==1,"echo exactly one frame");
  const Packet& frame=outgoing[0];const auto* ip=frame.data()+14;const auto* packet=ip+20;
  require(read16(ip+2)==20+8+payload.size() && sum(ip,20)==0,"echo IP length and checksum");
  require(read16(packet)==kNetworkEchoPort && read16(packet+2)==51000,"echo reverses ports");
  require(read16(packet+4)==payload.size()+8 && udp_sum(guest_ip,peer_ip,packet,8+payload.size())==0,"echo UDP checksum including odd byte");
  require(std::equal(payload.begin(),payload.end(),packet+8),"echo preserves binary payload");
}
int main() {
  require(network_initialize(reinterpret_cast<PageAllocator*>(uintptr_t{1})),"mock NIC initialization");
  uint32_t address=0;char text[16];
  require(network_parse_ipv4("255.1.2.0",&address) && network_format_ipv4(address,text,sizeof(text)) &&
          std::strcmp(text,"255.1.2.0")==0,"IPv4 parse/format");
  for(const char* invalid:{"","1.2.3","1.2.3.4x","1.2.3.256","-1.2.3.4","1..3.4","1000.2.3.4"})
    require(!network_parse_ipv4(invalid,&address),"reject malformed IPv4");
  require(!network_format_ipv4(guest_ip,text,15),"reject undersized formatting buffer");
  outgoing.clear();deliver(arp(1));require(outgoing.size()==1 && read16(outgoing[0].data()+20)==2,"ARP response to local address");
  for(size_t bytes:{size_t{0},size_t{1},size_t{257},kNetworkUdpMaxPayload}) {
    Packet payload(bytes);for(size_t i=0;i<bytes;++i)payload[i]=static_cast<uint8_t>(i*37);
    validate_echo(payload);
  }
  outgoing.clear();uint64_t invalid=network_status().invalid_packets;
  Packet bad=udp(kNetworkEchoPort,Packet(31,7));bad[bad.size()-1]^=1;deliver(bad);
  require(outgoing.empty() && network_status().invalid_packets==invalid+1,"bad UDP checksum never echoed");
  bad=udp(kNetworkEchoPort,Packet(9,3));bad[22]^=1;deliver(bad);
  require(outgoing.empty() && network_status().invalid_packets==invalid+2,"bad IP checksum never echoed");
  bad=udp(kNetworkEchoPort,Packet(12,9));put16(bad.data()+38,21);deliver(bad);
  require(outgoing.empty() && network_status().invalid_packets==invalid+3,"bad UDP declared length rejected");
  bad=udp(kNetworkEchoPort,Packet(15,1));put16(bad.data()+20,0x2000);put16(bad.data()+24,0);
  put16(bad.data()+24,sum(bad.data()+14,20));deliver(bad);require(outgoing.empty(),"fragment never echoed");
  bad=udp(kNetworkEchoPort,Packet(1201,2));deliver(bad);require(outgoing.empty(),"oversized UDP payload bounded");
  deliver(udp(kNetworkEchoPort,Packet(5,8),false));require(outgoing.size()==1,"IPv4 zero UDP checksum accepted");

  const int32_t socket=network_udp_open(9100,100);require(socket>0,"open owned UDP socket");
  require(!network_udp_close(socket,101),"another PID cannot close socket");
  require(network_udp_send(socket,peer_ip,51000,nullptr,0,1000,101)==-2,"another PID cannot send");
  NetworkDatagram metadata{};uint8_t buffer[1200];
  require(network_udp_receive(socket,&metadata,buffer,sizeof(buffer),101)==-2,"another PID cannot receive");
  for(unsigned i=0;i<5;++i)deliver(udp(9100,Packet(3,static_cast<uint8_t>(i))));
  const uint64_t dropped=network_status().udp_dropped;
  require(network_udp_receive(socket,&metadata,buffer,2,100)==-2,"small receive keeps datagram");
  for(unsigned i=0;i<4;++i) {
    require(network_udp_receive(socket,&metadata,buffer,sizeof(buffer),100)==3 && buffer[0]==i &&
            metadata.source_port==51000 && metadata.payload_bytes==3,"bounded FIFO preserves order and metadata");
  }
  require(network_udp_receive(socket,&metadata,buffer,sizeof(buffer),100)==-1,"empty socket reports would block");
  deliver(udp(9100,Packet{}));require(network_udp_receive(socket,&metadata,nullptr,0,100)==0,"zero-length datagram distinct from would block");
  require(network_udp_close(socket,100),"owner closes socket");
  const int32_t replacement=network_udp_open(9100,100);
  require(replacement>0 && replacement!=socket && !network_udp_close(socket,100),"stale generation rejected");
  network_udp_close_owner(100);require(!network_udp_close(replacement,100),"exit closes all owned sockets");
  int32_t handles[4];for(unsigned i=0;i<4;++i){handles[i]=network_udp_open(9200+i,200);require(handles[i]>0,"open bounded slots");}
  require(network_udp_open(9300,201)==-1,"fifth socket rejected");network_udp_close_owner(200);
  require(network_status().udp_dropped>=dropped,"drop counter retained");
  NetworkPingResult ping{};require(network_ping(peer_ip,100,&ping) && ping.sent && ping.replied,"ICMP request and checksum-correct reply");
  automatic_answers=false;require(!network_ping(network_ipv4(10,0,2,77),30,&ping),"ARP timeout finite without reply");

  // 强制某些随机包进入本机 Ethernet/IP 路径，ASan/UBSan 能检出长度读取越界。
  std::mt19937 random(641213);
  for(unsigned i=0;i<10000;++i) {
    Packet frame(random()%1530);for(auto& value:frame)value=static_cast<uint8_t>(random());
    if(frame.size()>=14){std::memcpy(frame.data(),guest_mac,6);std::memcpy(frame.data()+6,peer_mac,6);put16(frame.data()+12,i%2?0x800:0x806);}
    deliver(std::move(frame));
  }
  require(network_status().invalid_packets>1000,"fuzz packets classified without crashing");
  std::cout<<"Network host tests passed: checksums, malformed lengths, ARP/ICMP, binary UDP, PID ownership, generation, bounded FIFO, 10000 frames\n";
}
