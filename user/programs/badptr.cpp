#include "os64.hpp"

namespace {
// user.ld 把 const 数据放在 RX 段。内核可以从这里读，但不能把输出写进来。
const char read_only[256] = "This page must remain read-only.";
constexpr uint64_t bad_address = 0x10000000;
constexpr uint64_t kernel_address = 0x10000;
constexpr uint64_t guard_address = 0x7ef000;
// 用户栈最后四字节仍存在，后面的页已在用户窗口之外。
constexpr uint64_t stack_end_crossing = 0x7ffffc;
constexpr uint64_t high_bit = 1ULL << 32;

bool rejected(int64_t result, int64_t expected, const char* test) {
  if (result == expected) return true;
  error("badptr: unexpected result for "); error(test); error("\n");
  return false;
}

bool ipc_boundaries() {
  const uint64_t invalid_outputs[] = {
    0, bad_address, kernel_address, guard_address, stack_end_crossing,
    UINT64_MAX - 3, reinterpret_cast<uint64_t>(read_only),
  };
  for (uint64_t address : invalid_outputs)
    if (!rejected(syscall(22, address), -1, "pipe output")) return false;

  // 先保留真实端点，验证失败的 dup2 不会关闭已有的新 fd 或改动原引用。
  int32_t ends[2];
  if (pipe(ends) != 0) return false;
  bool ok = rejected(syscall(23, high_bit | static_cast<uint32_t>(ends[0]), ends[1]),
                     -1, "dup2 high old fd") &&
            rejected(syscall(23, ends[0], high_bit | static_cast<uint32_t>(ends[1])),
                     -1, "dup2 high new fd") &&
            rejected(syscall(23, ends[0], 19), -1, "dup2 fd outside table") &&
            rejected(syscall(23, UINT64_MAX, ends[1]), -1, "dup2 negative old fd") &&
            rejected(syscall(24, high_bit | static_cast<uint32_t>(ends[0])),
                     -1, "dup high fd") &&
            rejected(syscall(24, 19), -1, "dup fd outside table") &&
            rejected(syscall(24, UINT64_MAX), -1, "dup negative fd");
  char byte = 0;
  ok = ok && write(ends[1], "K", 1) == 1 && read(ends[0], &byte, 1) == 1 && byte == 'K';
  close(ends[0]); close(ends[1]);
  return ok;
}

bool performance_boundaries() {
  const uint64_t invalid_outputs[] = {
    0, bad_address, kernel_address, guard_address, stack_end_crossing,
    UINT64_MAX - 63, reinterpret_cast<uint64_t>(read_only),
  };
  for (uint64_t address : invalid_outputs) {
    if (!rejected(syscall(31, address, 1), -1, "log output") ||
        !rejected(syscall(32, address), -1, "performance output")) return false;
  }
  KernelLogRecord record{};
  if (!rejected(read_log(&record, 65), -1, "log excessive count") ||
      !rejected(read_log(&record, high_bit | 1), -1, "log high count") ||
      !rejected(syscall(31, bad_address, 0), 0, "zero-count log")) return false;
  // 拒绝后，再用合法的缓冲检查接口仍可用。零条日志也可以是成功结果。
  PerformanceSnapshot snapshot{};
  return read_log(&record, 1) >= 0 && perf_snapshot(&snapshot) == 0 &&
         snapshot.abi_version == 1 && snapshot.timer_hz > 0;
}

bool udp_boundaries() {
  // 参数预检在“有没有网卡”之前执行；这些检查在无 NIC 的虚拟机也应返回 -2。
  // 这里没有真的打开 socket，因此假 handle 1 不会消耗网络资源。
  if (!rejected(syscall(36, 0), -2, "UDP zero port") ||
      !rejected(syscall(36, high_bit | 8080), -2, "UDP high port") ||
      !rejected(syscall(37, high_bit | 1), -2, "UDP high handle")) return false;
  const uint64_t invalid_inputs[] = {
    0, bad_address, kernel_address, guard_address, stack_end_crossing, UINT64_MAX - 3,
  };
  for (uint64_t address : invalid_inputs)
    if (!rejected(syscall(38, 1, 0x0a000202, 8080, address, 8),
                  -2, "UDP send input")) return false;
  const uint64_t input = reinterpret_cast<uint64_t>(read_only);
  if (!rejected(syscall(38, 1, high_bit | 0x0a000202, 8080, input, 1),
                -2, "UDP high IPv4") ||
      !rejected(syscall(38, 1, 0x0a000202, 65536, input, 1), -2, "UDP large port") ||
      !rejected(syscall(38, 1, 0x0a000202, 8080, input, 1201), -2, "UDP large payload") ||
      !rejected(syscall(38, 1, 0x0a000202, 8080, input, high_bit | 1),
                -2, "UDP high payload size")) return false;
  alignas(8) unsigned char metadata[12] = {};
  char payload[8] = {};
  const uint64_t valid_metadata = reinterpret_cast<uint64_t>(metadata);
  const uint64_t valid_payload = reinterpret_cast<uint64_t>(payload);
  const uint64_t invalid_outputs[] = {
    0, bad_address, kernel_address, guard_address, stack_end_crossing,
    UINT64_MAX - 3, reinterpret_cast<uint64_t>(read_only),
  };
  for (uint64_t address : invalid_outputs) {
    if (!rejected(syscall(39, 1, address, valid_payload, sizeof(payload)),
                  -2, "UDP receive metadata") ||
        !rejected(syscall(39, 1, valid_metadata, address, sizeof(payload)),
                  -2, "UDP receive payload")) return false;
  }
  return rejected(syscall(39, 1, valid_metadata, valid_payload, 1201),
                  -2, "UDP large receive capacity") &&
         rejected(syscall(39, 1, valid_metadata, valid_payload, high_bit | 1),
                  -2, "UDP high receive capacity");
}
}

extern "C" int main(int, char**) {
  int64_t result=write(1,reinterpret_cast<const void*>(0x10000000),32);
  if(result>=0) { error("badptr: invalid pointer was accepted\n"); return 1; }
  print("badptr rejected\n");
  result=open(reinterpret_cast<const char*>(0x10000000));
  if(result>=0) { close(result); return 2; }
  print("badptr path rejected\n");
  result=open("/readme.txt",(1ULL<<32)|READ);
  if(result!=-1) { if(result>=0) close(result); return 3; }
  print("badptr flags rejected\n");
  // replace_file 也必须经过用户边界校验，不能让坏参数进入磁盘事务。
  result=replace_file(reinterpret_cast<const char*>(0x10000000),"safe",4);
  if(result!=-1) return 4;
  print("badptr replace path rejected\n");
  result=replace_file("/readme.txt",reinterpret_cast<const void*>(0x10000000),32);
  if(result!=-1) return 5;
  result=replace_file("/readme.txt",nullptr,1);
  if(result!=-1) return 6;
  result=replace_file("/readme.txt",reinterpret_cast<const void*>(UINTPTR_MAX-7),16);
  if(result!=-1) return 7;
  print("badptr replace buffer rejected\n");
  result=replace_file("/readme.txt","safe",69633);
  if(result!=-1) return 8;
  result=replace_file("/readme.txt","safe",(1ULL<<32)|4);
  if(result!=-1) return 9;
  print("badptr replace size rejected\n");
  if (!ipc_boundaries()) return 10;
  print("badptr IPC boundaries rejected\n");
  if (!performance_boundaries()) return 11;
  print("badptr performance boundaries rejected\n");
  if (!udp_boundaries()) return 12;
  print("badptr UDP boundaries rejected\n");
  return 0;
}
